/*
 * Unidesk - file operations of the SFTP tab, on this computer and on the server
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define _FILE_OFFSET_BITS 64
#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "remmina_ftp_fs.h"
#ifdef HAVE_LIBSSH
#include "remmina_ssh.h"
#endif

struct _RemminaFTPFs {
	gboolean	local;
#ifdef HAVE_LIBSSH
	RemminaSFTP *	sftp;
#endif
};

struct _RemminaFTPStream {
	RemminaFTPFs *	fs;
	FILE *		fp;
#ifdef HAVE_LIBSSH
	sftp_file	sf;
#endif
	gchar *		path;
};

/* ---- Entries and paths ---- */

RemminaFTPEntry *remmina_ftp_entry_new(void)
{
	return g_new0(RemminaFTPEntry, 1);
}

RemminaFTPEntry *remmina_ftp_entry_copy(const RemminaFTPEntry *entry)
{
	RemminaFTPEntry *copy = g_new(RemminaFTPEntry, 1);

	*copy = *entry;
	copy->name = g_strdup(entry->name);
	copy->owner = g_strdup(entry->owner);
	copy->group = g_strdup(entry->group);
	return copy;
}

static void remmina_ftp_entry_clear(RemminaFTPEntry *entry)
{
	g_free(entry->name);
	g_free(entry->owner);
	g_free(entry->group);
	memset(entry, 0, sizeof(*entry));
}

void remmina_ftp_entry_free(RemminaFTPEntry *entry)
{
	if (!entry)
		return;
	remmina_ftp_entry_clear(entry);
	g_free(entry);
}

gchar *remmina_ftp_path_join(const gchar *dir, const gchar *name)
{
	if (!dir || !dir[0])
		return g_strconcat("/", name, NULL);
	if (g_str_has_suffix(dir, "/"))
		return g_strconcat(dir, name, NULL);
	return g_strconcat(dir, "/", name, NULL);
}

gchar *remmina_ftp_path_parent(const gchar *path)
{
	gchar *parent = g_path_get_dirname(path);

	/* g_path_get_dirname("/") is "/", of "/usr" too */
	if (!parent[0] || g_strcmp0(parent, ".") == 0) {
		g_free(parent);
		return g_strdup("/");
	}
	return parent;
}

const gchar *remmina_ftp_path_basename(const gchar *path)
{
	const gchar *slash = strrchr(path, '/');

	return slash && slash[1] ? slash + 1 : path;
}

static void remmina_ftp_set_errno_error(GError **error, const gchar *what, const gchar *path, int err)
{
	gchar *display = g_filename_display_name(path);

	/* TRANSLATORS: an error of the SFTP tab: what failed, the path, why ("Could not delete “/a”: …") */
	g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err), _("%s “%s”: %s"), what, display, g_strerror(err));
	g_free(display);
}

/* ---- File systems ---- */

RemminaFTPFs *remmina_ftp_fs_new_local(void)
{
	RemminaFTPFs *fs = g_new0(RemminaFTPFs, 1);

	fs->local = TRUE;
	return fs;
}

RemminaFTPFs *remmina_ftp_fs_new_remote(gpointer sftp)
{
	RemminaFTPFs *fs = g_new0(RemminaFTPFs, 1);

	fs->local = FALSE;
#ifdef HAVE_LIBSSH
	fs->sftp = (RemminaSFTP *)sftp;
#endif
	return fs;
}

void remmina_ftp_fs_free(RemminaFTPFs *fs)
{
	g_free(fs);
}

gboolean remmina_ftp_fs_is_local(RemminaFTPFs *fs)
{
	return fs->local;
}

#ifdef HAVE_LIBSSH
/* Paths go to the server in its character set, names come back in UTF-8 */
static gchar *remote_path(RemminaFTPFs *fs, const gchar *path)
{
	return remmina_ssh_unconvert(REMMINA_SSH(fs->sftp), path);
}

static void remote_error(RemminaFTPFs *fs, GError **error, const gchar *what, const gchar *path)
{
	const char *msg = ssh_get_error(REMMINA_SSH(fs->sftp)->session);
	int code = sftp_get_error(fs->sftp->sftp_sess);

	switch (code) {
	case SSH_FX_NO_SUCH_FILE:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("%s “%s”: %s"), what, path, _("No such file or folder"));
		break;
	case SSH_FX_PERMISSION_DENIED:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("%s “%s”: %s"), what, path, _("Permission denied"));
		break;
	default:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("%s “%s”: %s"), what, path, msg && msg[0] ? msg : _("Server error"));
		break;
	}
}

static void remote_fill_entry(RemminaFTPFs *fs, sftp_attributes attr, RemminaFTPEntry *entry)
{
	entry->is_link = (attr->type == SSH_FILEXFER_TYPE_SYMLINK) || ((attr->permissions & S_IFMT) == S_IFLNK);
	entry->type = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY || (attr->permissions & S_IFMT) == S_IFDIR) ?
		      REMMINA_FTP_ENTRY_DIR : REMMINA_FTP_ENTRY_FILE;
	entry->size = attr->size;
	entry->mtime = attr->mtime64 ? (gint64)attr->mtime64 : (gint64)attr->mtime;
	entry->mode = attr->permissions & 07777;
	entry->owner = attr->owner ? remmina_ssh_convert(REMMINA_SSH(fs->sftp), attr->owner) :
		       g_strdup_printf("%u", attr->uid);
	entry->group = attr->group ? remmina_ssh_convert(REMMINA_SSH(fs->sftp), attr->group) :
		       g_strdup_printf("%u", attr->gid);
}

/* A link to a folder opens like a folder */
static void remote_resolve_link(RemminaFTPFs *fs, const gchar *rpath, RemminaFTPEntry *entry)
{
	sftp_attributes target;

	if (!entry->is_link)
		return;
	target = sftp_stat(fs->sftp->sftp_sess, rpath);
	if (target) {
		entry->type = (target->type == SSH_FILEXFER_TYPE_DIRECTORY ||
			       (target->permissions & S_IFMT) == S_IFDIR) ? REMMINA_FTP_ENTRY_DIR : REMMINA_FTP_ENTRY_FILE;
		if (entry->type == REMMINA_FTP_ENTRY_FILE)
			entry->size = target->size;
		sftp_attributes_free(target);
	} else {
		entry->type = REMMINA_FTP_ENTRY_FILE;
	}
}
#endif

static void local_fill_entry(const struct stat *st, RemminaFTPEntry *entry)
{
	entry->is_link = S_ISLNK(st->st_mode);
	entry->type = S_ISDIR(st->st_mode) ? REMMINA_FTP_ENTRY_DIR : REMMINA_FTP_ENTRY_FILE;
	entry->size = (guint64)st->st_size;
	entry->mtime = (gint64)st->st_mtime;
	entry->mode = st->st_mode & 07777;
}

static void local_resolve_link(const gchar *path, RemminaFTPEntry *entry)
{
	struct stat st;

	if (!entry->is_link)
		return;
	if (g_stat(path, &st) == 0) {
		entry->type = S_ISDIR(st.st_mode) ? REMMINA_FTP_ENTRY_DIR : REMMINA_FTP_ENTRY_FILE;
		if (entry->type == REMMINA_FTP_ENTRY_FILE)
			entry->size = (guint64)st.st_size;
	} else {
		entry->type = REMMINA_FTP_ENTRY_FILE;
	}
}

GPtrArray *remmina_ftp_fs_list(RemminaFTPFs *fs, const gchar *dir, GError **error)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_ftp_entry_free);

	if (fs->local) {
		GDir *d = g_dir_open(dir, 0, error);
		const gchar *name;

		if (!d) {
			g_ptr_array_unref(entries);
			return NULL;
		}
		while ((name = g_dir_read_name(d)) != NULL) {
			gchar *path = remmina_ftp_path_join(dir, name);
			struct stat st;

			if (g_lstat(path, &st) == 0) {
				RemminaFTPEntry *entry = remmina_ftp_entry_new();
				entry->name = g_strdup(name);
				local_fill_entry(&st, entry);
				local_resolve_link(path, entry);
				g_ptr_array_add(entries, entry);
			}
			g_free(path);
		}
		g_dir_close(d);
		return entries;
	}

#ifdef HAVE_LIBSSH
	gchar *rdir = remote_path(fs, dir);
	sftp_dir sd = sftp_opendir(fs->sftp->sftp_sess, rdir);
	sftp_attributes attr;

	if (!sd) {
		remote_error(fs, error, _("Could not open the folder"), dir);
		g_free(rdir);
		g_ptr_array_unref(entries);
		return NULL;
	}
	while ((attr = sftp_readdir(fs->sftp->sftp_sess, sd)) != NULL) {
		if (g_strcmp0(attr->name, ".") != 0 && g_strcmp0(attr->name, "..") != 0) {
			RemminaFTPEntry *entry = remmina_ftp_entry_new();
			entry->name = remmina_ssh_convert(REMMINA_SSH(fs->sftp), attr->name);
			remote_fill_entry(fs, attr, entry);
			if (entry->is_link) {
				gchar *rpath = remmina_ftp_path_join(rdir, attr->name);
				remote_resolve_link(fs, rpath, entry);
				g_free(rpath);
			}
			g_ptr_array_add(entries, entry);
		}
		sftp_attributes_free(attr);
	}
	if (!sftp_dir_eof(sd)) {
		remote_error(fs, error, _("Could not read the folder"), dir);
		sftp_closedir(sd);
		g_free(rdir);
		g_ptr_array_unref(entries);
		return NULL;
	}
	sftp_closedir(sd);
	g_free(rdir);
	return entries;
#else
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "SFTP is not available");
	g_ptr_array_unref(entries);
	return NULL;
#endif
}

gchar *remmina_ftp_fs_realpath(RemminaFTPFs *fs, const gchar *path, GError **error)
{
	if (fs->local) {
		char *real = realpath(path, NULL);
		gchar *ret;

		if (!real) {
			remmina_ftp_set_errno_error(error, _("Could not open the folder"), path, errno);
			return NULL;
		}
		ret = g_strdup(real);
		free(real);
		return ret;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	char *canon = sftp_canonicalize_path(fs->sftp->sftp_sess, rpath);
	gchar *ret;

	g_free(rpath);
	if (!canon) {
		remote_error(fs, error, _("Could not open the folder"), path);
		return NULL;
	}
	ret = remmina_ssh_convert(REMMINA_SSH(fs->sftp), canon);
	ssh_string_free_char(canon);
	return ret;
#else
	return NULL;
#endif
}

gboolean remmina_ftp_fs_stat(RemminaFTPFs *fs, const gchar *path, RemminaFTPEntry *entry, GError **error)
{
	remmina_ftp_entry_clear(entry);
	entry->name = g_strdup(remmina_ftp_path_basename(path));

	if (fs->local) {
		struct stat st;
		struct passwd *pw;
		struct group *gr;

		if (g_lstat(path, &st) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not read"), path, errno);
			return FALSE;
		}
		local_fill_entry(&st, entry);
		local_resolve_link(path, entry);
		pw = getpwuid(st.st_uid);
		gr = getgrgid(st.st_gid);
		entry->owner = pw ? g_strdup(pw->pw_name) : g_strdup_printf("%u", (guint)st.st_uid);
		entry->group = gr ? g_strdup(gr->gr_name) : g_strdup_printf("%u", (guint)st.st_gid);
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	sftp_attributes attr = sftp_lstat(fs->sftp->sftp_sess, rpath);

	if (!attr) {
		remote_error(fs, error, _("Could not read"), path);
		g_free(rpath);
		return FALSE;
	}
	remote_fill_entry(fs, attr, entry);
	remote_resolve_link(fs, rpath, entry);
	sftp_attributes_free(attr);
	g_free(rpath);
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_mkdir(RemminaFTPFs *fs, const gchar *path, GError **error)
{
	if (fs->local) {
		if (g_mkdir(path, 0755) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not create the folder"), path, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	int ret = sftp_mkdir(fs->sftp->sftp_sess, rpath, 0755);

	g_free(rpath);
	if (ret != 0) {
		remote_error(fs, error, _("Could not create the folder"), path);
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_remove(RemminaFTPFs *fs, const gchar *path, gboolean is_dir, GError **error)
{
	if (fs->local) {
		if ((is_dir ? g_rmdir(path) : g_unlink(path)) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not delete"), path, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	int ret = is_dir ? sftp_rmdir(fs->sftp->sftp_sess, rpath) : sftp_unlink(fs->sftp->sftp_sess, rpath);

	g_free(rpath);
	if (ret != 0) {
		remote_error(fs, error, _("Could not delete"), path);
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_rename(RemminaFTPFs *fs, const gchar *from, const gchar *to, GError **error)
{
	if (fs->local) {
		if (g_rename(from, to) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not rename"), from, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rfrom = remote_path(fs, from);
	gchar *rto = remote_path(fs, to);
	int ret = sftp_rename(fs->sftp->sftp_sess, rfrom, rto);

	g_free(rfrom);
	g_free(rto);
	if (ret != 0) {
		remote_error(fs, error, _("Could not rename"), from);
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_chmod(RemminaFTPFs *fs, const gchar *path, guint32 mode, GError **error)
{
	if (fs->local) {
		if (g_chmod(path, (int)(mode & 07777)) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not change the attributes of"), path, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	int ret = sftp_chmod(fs->sftp->sftp_sess, rpath, (mode_t)(mode & 07777));

	g_free(rpath);
	if (ret != 0) {
		remote_error(fs, error, _("Could not change the attributes of"), path);
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_set_mtime(RemminaFTPFs *fs, const gchar *path, gint64 mtime, GError **error)
{
	if (fs->local) {
		struct timespec ts[2] = { { 0, UTIME_OMIT }, { (time_t)mtime, 0 } };

		if (utimensat(AT_FDCWD, path, ts, 0) != 0) {
			remmina_ftp_set_errno_error(error, _("Could not change the date of"), path, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	struct timeval tv[2] = { { (time_t)mtime, 0 }, { (time_t)mtime, 0 } };
	int ret = sftp_utimes(fs->sftp->sftp_sess, rpath, tv);

	g_free(rpath);
	if (ret != 0) {
		remote_error(fs, error, _("Could not change the date of"), path);
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean remmina_ftp_fs_statvfs(RemminaFTPFs *fs, const gchar *path, guint64 *avail, guint64 *total)
{
	if (fs->local) {
		struct statvfs st;

		if (statvfs(path, &st) != 0)
			return FALSE;
		*avail = (guint64)st.f_bavail * st.f_frsize;
		*total = (guint64)st.f_blocks * st.f_frsize;
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	if (!sftp_extension_supported(fs->sftp->sftp_sess, "statvfs@openssh.com", "2"))
		return FALSE;
	gchar *rpath = remote_path(fs, path);
	sftp_statvfs_t st = sftp_statvfs(fs->sftp->sftp_sess, rpath);

	g_free(rpath);
	if (!st)
		return FALSE;
	*avail = st->f_bavail * st->f_frsize;
	*total = st->f_blocks * st->f_frsize;
	sftp_statvfs_free(st);
	return TRUE;
#else
	return FALSE;
#endif
}

/* ---- Streams ---- */

RemminaFTPStream *remmina_ftp_fs_open_read(RemminaFTPFs *fs, const gchar *path, guint64 offset, GError **error)
{
	RemminaFTPStream *stream = g_new0(RemminaFTPStream, 1);

	stream->fs = fs;
	stream->path = g_strdup(path);
	if (fs->local) {
		stream->fp = g_fopen(path, "rb");
		if (!stream->fp || (offset && fseeko(stream->fp, (off_t)offset, SEEK_SET) != 0)) {
			remmina_ftp_set_errno_error(error, _("Could not open the file"), path, errno);
			remmina_ftp_stream_close(stream);
			return NULL;
		}
		return stream;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	stream->sf = sftp_open(fs->sftp->sftp_sess, rpath, O_RDONLY, 0);
	g_free(rpath);
	if (!stream->sf || (offset && sftp_seek64(stream->sf, offset) != 0)) {
		remote_error(fs, error, _("Could not open the file"), path);
		remmina_ftp_stream_close(stream);
		return NULL;
	}
	return stream;
#else
	remmina_ftp_stream_close(stream);
	return NULL;
#endif
}

RemminaFTPStream *remmina_ftp_fs_open_write(RemminaFTPFs *fs, const gchar *path, gboolean append, GError **error)
{
	RemminaFTPStream *stream = g_new0(RemminaFTPStream, 1);

	stream->fs = fs;
	stream->path = g_strdup(path);
	if (fs->local) {
		stream->fp = g_fopen(path, append ? "ab" : "wb");
		if (!stream->fp) {
			remmina_ftp_set_errno_error(error, _("Could not create the file"), path, errno);
			remmina_ftp_stream_close(stream);
			return NULL;
		}
		return stream;
	}
#ifdef HAVE_LIBSSH
	gchar *rpath = remote_path(fs, path);
	stream->sf = sftp_open(fs->sftp->sftp_sess, rpath, O_WRONLY | O_CREAT | (append ? 0 : O_TRUNC), 0644);
	g_free(rpath);
	if (!stream->sf) {
		remote_error(fs, error, _("Could not create the file"), path);
		remmina_ftp_stream_close(stream);
		return NULL;
	}
	if (append) {
		sftp_attributes attr = sftp_fstat(stream->sf);
		guint64 size = attr ? attr->size : 0;

		if (attr)
			sftp_attributes_free(attr);
		if (size && sftp_seek64(stream->sf, size) != 0) {
			remote_error(fs, error, _("Could not open the file"), path);
			remmina_ftp_stream_close(stream);
			return NULL;
		}
	}
	return stream;
#else
	remmina_ftp_stream_close(stream);
	return NULL;
#endif
}

gssize remmina_ftp_stream_read(RemminaFTPStream *stream, gpointer buf, gsize len, GError **error)
{
	if (stream->fp) {
		size_t n = fread(buf, 1, len, stream->fp);

		if (n == 0 && ferror(stream->fp)) {
			remmina_ftp_set_errno_error(error, _("Could not read the file"), stream->path, errno);
			return -1;
		}
		return (gssize)n;
	}
#ifdef HAVE_LIBSSH
	ssize_t n = sftp_read(stream->sf, buf, len);

	if (n < 0) {
		remote_error(stream->fs, error, _("Could not read the file"), stream->path);
		return -1;
	}
	return (gssize)n;
#else
	return -1;
#endif
}

gboolean remmina_ftp_stream_write(RemminaFTPStream *stream, gconstpointer buf, gsize len, GError **error)
{
	if (stream->fp) {
		if (fwrite(buf, 1, len, stream->fp) != len) {
			remmina_ftp_set_errno_error(error, _("Could not write the file"), stream->path, errno);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HAVE_LIBSSH
	const gchar *p = buf;

	while (len > 0) {
		ssize_t n = sftp_write(stream->sf, p, len);

		if (n <= 0) {
			remote_error(stream->fs, error, _("Could not write the file"), stream->path);
			return FALSE;
		}
		p += n;
		len -= (gsize)n;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

void remmina_ftp_stream_close(RemminaFTPStream *stream)
{
	if (!stream)
		return;
	if (stream->fp)
		fclose(stream->fp);
#ifdef HAVE_LIBSSH
	if (stream->sf)
		sftp_close(stream->sf);
#endif
	g_free(stream->path);
	g_free(stream);
}
