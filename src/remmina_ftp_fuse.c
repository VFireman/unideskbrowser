/*
 * Unidesk - server files of the SFTP tab, lent to other programs through FUSE
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
#define FUSE_USE_VERSION 31

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fuse.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "remmina_ftp_fuse.h"

#define MOUNT_PREFIX "unidesk-sftp-drag."

struct _RemminaFTPFuse {
	gint			ref;	/* the owner's and the thread's */
	RemminaFTPFuseOpenFs	open_fs;
	RemminaFTPFuseCloseFs	close_fs;
	gpointer		data;

	GMutex			lock;
	GHashTable *		exports;	/* "<k>" → GHashTable (name → server path); under lock */
	guint			last_export;
	gboolean		connecting;	/* the thread is in open_fs, which uses data; under lock */

	gchar *			mountpoint;
	struct fuse *		fuse;
	GThread *		thread;
	time_t			started;
	gboolean		unmounted;
	gint			thread_done;	/* atomic: the thread has closed its session */

	/* The FUSE thread's own */
	RemminaFTPFs *		fs;
	gpointer		session;
};

/* An open server file: reads go on from where the last one stopped */
typedef struct {
	gchar *			remote;
	RemminaFTPStream *	stream;
	off_t			offset;
} OpenFile;

typedef enum {
	NODE_NONE,
	NODE_ROOT,	/* "/": one folder per drag */
	NODE_EXPORT,	/* "/<k>": the dragged files and folders */
	NODE_REMOTE,	/* "/<k>/<name>[/…]": on the server */
} NodeKind;

static void ffs_unref(RemminaFTPFuse *f)
{
	if (!g_atomic_int_dec_and_test(&f->ref))
		return;
	if (f->fuse)
		fuse_destroy(f->fuse);
	if (f->mountpoint) {
		g_rmdir(f->mountpoint);
		g_free(f->mountpoint);
	}
	g_hash_table_unref(f->exports);
	g_mutex_clear(&f->lock);
	g_free(f);
}

/* ---- The file system (FUSE thread) ---- */

static RemminaFTPFuse *ffs_self(void)
{
	return fuse_get_context()->private_data;
}

static NodeKind ffs_resolve(RemminaFTPFuse *f, const char *path, gchar **remote)
{
	NodeKind kind = NODE_NONE;
	GHashTable *names;
	gchar **parts;

	*remote = NULL;
	if (strcmp(path, "/") == 0)
		return NODE_ROOT;
	parts = g_strsplit(path + 1, "/", 3);
	g_mutex_lock(&f->lock);
	names = g_hash_table_lookup(f->exports, parts[0]);
	if (names && !parts[1]) {
		kind = NODE_EXPORT;
	} else if (names) {
		const gchar *base = g_hash_table_lookup(names, parts[1]);
		if (base) {
			kind = NODE_REMOTE;
			*remote = parts[2] && parts[2][0] ? remmina_ftp_path_join(base, parts[2]) : g_strdup(base);
		}
	}
	g_mutex_unlock(&f->lock);
	g_strfreev(parts);
	return kind;
}

static int ffs_errno(GError **error)
{
	int result = -EIO;

	if (*error && (*error)->domain == G_IO_ERROR) {
		switch ((*error)->code) {
		case G_IO_ERROR_NOT_FOUND:
			result = -ENOENT;
			break;
		case G_IO_ERROR_PERMISSION_DENIED:
			result = -EACCES;
			break;
		case G_IO_ERROR_NOT_DIRECTORY:
			result = -ENOTDIR;
			break;
		default:
			break;
		}
	}
	g_clear_error(error);
	return result;
}

static int ffs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
	RemminaFTPFuse *f = ffs_self();
	RemminaFTPEntry entry = { 0 };
	GError *error = NULL;
	gchar *remote;
	NodeKind kind = ffs_resolve(f, path, &remote);
	gboolean dir;

	memset(st, 0, sizeof(*st));
	st->st_uid = getuid();
	st->st_gid = getgid();
	if (kind == NODE_ROOT || kind == NODE_EXPORT) {
		st->st_mode = S_IFDIR | 0555;
		st->st_nlink = 2;
		st->st_mtime = st->st_atime = st->st_ctime = f->started;
		return 0;
	}
	if (kind == NODE_NONE)
		return -ENOENT;
	if (!f->fs) {
		g_free(remote);
		return -EIO;
	}
	if (!remmina_ftp_fs_stat(f->fs, remote, &entry, &error)) {
		g_free(remote);
		return ffs_errno(&error);
	}
	/* Links show what they point to, as the copy jobs follow them */
	dir = entry.type == REMMINA_FTP_ENTRY_DIR;
	st->st_mode = (dir ? S_IFDIR : S_IFREG) | (entry.mode & 0777);
	st->st_nlink = dir ? 2 : 1;
	st->st_size = dir ? 0 : (off_t)entry.size;
	st->st_blksize = 64 * 1024;
	st->st_blocks = (st->st_size + 511) / 512;
	st->st_mtime = st->st_atime = st->st_ctime = (time_t)entry.mtime;
	g_free(entry.name);
	g_free(entry.owner);
	g_free(entry.group);
	g_free(remote);
	return 0;
}

static int ffs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	RemminaFTPFuse *f = ffs_self();
	GError *error = NULL;
	gchar *remote;
	NodeKind kind = ffs_resolve(f, path, &remote);
	GHashTableIter iter;
	gpointer key;

	if (kind == NODE_NONE)
		return -ENOENT;
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	if (kind == NODE_ROOT || kind == NODE_EXPORT) {
		g_mutex_lock(&f->lock);
		g_hash_table_iter_init(&iter, kind == NODE_ROOT ? f->exports : g_hash_table_lookup(f->exports, path + 1));
		while (g_hash_table_iter_next(&iter, &key, NULL))
			filler(buf, key, NULL, 0, 0);
		g_mutex_unlock(&f->lock);
	} else {
		GPtrArray *list = f->fs ? remmina_ftp_fs_list(f->fs, remote, &error) : NULL;
		guint i;

		g_free(remote);
		if (!list)
			return f->fs ? ffs_errno(&error) : -EIO;
		for (i = 0; i < list->len; i++)
			filler(buf, ((RemminaFTPEntry *)g_ptr_array_index(list, i))->name, NULL, 0, 0);
		g_ptr_array_unref(list);
	}
	return 0;
}

static int ffs_open(const char *path, struct fuse_file_info *fi)
{
	RemminaFTPFuse *f = ffs_self();
	GError *error = NULL;
	RemminaFTPStream *stream;
	OpenFile *of;
	gchar *remote;
	NodeKind kind;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EROFS;
	kind = ffs_resolve(f, path, &remote);
	if (kind != NODE_REMOTE)
		return kind == NODE_NONE ? -ENOENT : -EISDIR;
	if (!f->fs) {
		g_free(remote);
		return -EIO;
	}
	stream = remmina_ftp_fs_open_read(f->fs, remote, 0, &error);
	if (!stream) {
		g_free(remote);
		return ffs_errno(&error);
	}
	of = g_new0(OpenFile, 1);
	of->remote = remote;
	of->stream = stream;
	fi->fh = (uint64_t)(guintptr)of;
	return 0;
}

static int ffs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	RemminaFTPFuse *f = ffs_self();
	OpenFile *of = (OpenFile *)(guintptr)fi->fh;
	GError *error = NULL;
	size_t done = 0;

	if (!of->stream || offset != of->offset) {
		/* Not where the last read stopped: open again there */
		if (of->stream)
			remmina_ftp_stream_close(of->stream);
		of->stream = remmina_ftp_fs_open_read(f->fs, of->remote, (guint64)offset, &error);
		if (!of->stream)
			return ffs_errno(&error);
		of->offset = offset;
	}
	while (done < size) {
		gssize n = remmina_ftp_stream_read(of->stream, buf + done, size - done, &error);
		if (n < 0) {
			if (done)
				break;
			return ffs_errno(&error);
		}
		if (n == 0)
			break;
		done += (size_t)n;
	}
	g_clear_error(&error);
	of->offset += (off_t)done;
	return (int)done;
}

static int ffs_release(const char *path, struct fuse_file_info *fi)
{
	OpenFile *of = (OpenFile *)(guintptr)fi->fh;

	if (of) {
		if (of->stream)
			remmina_ftp_stream_close(of->stream);
		g_free(of->remote);
		g_free(of);
	}
	return 0;
}

static gpointer ffs_thread(gpointer data)
{
	RemminaFTPFuse *f = data;
	gpointer session = NULL;
	gchar *error = NULL;
	RemminaFTPFs *fs;

	/* Requests wait in the kernel until the loop runs */
	fs = f->open_fs(f->data, &session, &error);
	if (!fs)
		g_warning("SFTP drag and drop: %s", error ? error : "no connection");
	g_free(error);
	g_mutex_lock(&f->lock);
	f->fs = fs;
	f->session = session;
	f->connecting = FALSE;
	g_mutex_unlock(&f->lock);

	/* Until the file system is unmounted and no file is open any more */
	fuse_loop(f->fuse);

	if (f->fs)
		f->close_fs(f->fs, f->session);
	f->fs = NULL;
	g_atomic_int_set(&f->thread_done, 1);
	ffs_unref(f);
	return NULL;
}

/* ---- The owner's side (main thread) ---- */

static GList *ffs_mounted;	/* RemminaFTPFuse with a mount, for the application's end */

/* Quitting with SFTP tabs open does not always dispose them: leave no mount behind. The threads then
 * close their sessions: give them a moment, as exit() tears OpenSSL down (see rdp_plugin.c) */
static void ffs_on_app_shutdown(GApplication *app, gpointer data)
{
	gint64 deadline = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
	GList *l;

	for (l = ffs_mounted; l; l = l->next) {
		RemminaFTPFuse *f = l->data;
		if (f->fuse && !f->unmounted) {
			fuse_unmount(f->fuse);
			f->unmounted = TRUE;
		}
	}
	for (l = ffs_mounted; l; l = l->next) {
		RemminaFTPFuse *f = l->data;
		/* A program still copying keeps the thread busy: do not wait for ever */
		while (!g_atomic_int_get(&f->thread_done) && g_get_monotonic_time() < deadline)
			g_usleep(10 * 1000);
	}
}

RemminaFTPFuse *remmina_ftp_fuse_new(RemminaFTPFuseOpenFs open_fs, RemminaFTPFuseCloseFs close_fs, gpointer data)
{
	RemminaFTPFuse *f = g_new0(RemminaFTPFuse, 1);

	f->ref = 1;
	f->open_fs = open_fs;
	f->close_fs = close_fs;
	f->data = data;
	g_mutex_init(&f->lock);
	f->exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
	return f;
}

gboolean remmina_ftp_fuse_start(RemminaFTPFuse *f, GError **error)
{
	static const struct fuse_operations ops = {
		.getattr	= ffs_getattr,
		.readdir	= ffs_readdir,
		.open		= ffs_open,
		.read		= ffs_read,
		.release	= ffs_release,
	};
	static gint mounts = 0;
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

	if (f->thread)
		return TRUE;
	g_free(f->mountpoint);
	f->mountpoint = g_strdup_printf("%s/" MOUNT_PREFIX "%d.%d", g_get_tmp_dir(), (int)getpid(), g_atomic_int_add(&mounts, 1) + 1);
	if (g_mkdir(f->mountpoint, 0700) != 0 && errno != EEXIST) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), _("Could not create the folder “%s”: %s"),
			    f->mountpoint, g_strerror(errno));
		g_clear_pointer(&f->mountpoint, g_free);
		return FALSE;
	}
	fuse_opt_add_arg(&args, "unidesk");
	fuse_opt_add_arg(&args, "-o");
	fuse_opt_add_arg(&args, "ro,fsname=unidesk-sftp,subtype=unidesk");
	f->fuse = fuse_new(&args, &ops, sizeof(ops), f);
	fuse_opt_free_args(&args);
	if (!f->fuse || fuse_mount(f->fuse, f->mountpoint) != 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Could not lend the files to other programs: FUSE is not available."));
		if (f->fuse)
			fuse_destroy(f->fuse);
		f->fuse = NULL;
		g_rmdir(f->mountpoint);
		g_clear_pointer(&f->mountpoint, g_free);
		return FALSE;
	}
	f->started = time(NULL);
	f->connecting = TRUE;
	g_atomic_int_inc(&f->ref);
	f->thread = g_thread_new("unidesk-sftp-fuse", ffs_thread, f);
	if (!ffs_mounted && g_application_get_default())
		g_signal_connect(g_application_get_default(), "shutdown", G_CALLBACK(ffs_on_app_shutdown), NULL);
	ffs_mounted = g_list_prepend(ffs_mounted, f);
	return TRUE;
}

gchar **remmina_ftp_fuse_export(RemminaFTPFuse *f, GPtrArray *remote_paths, GError **error)
{
	GHashTable *names;
	gchar **uris;
	gchar *key;
	guint i, n = 0;

	if (!remmina_ftp_fuse_start(f, error))
		return NULL;
	names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	uris = g_new0(gchar *, remote_paths->len + 1);
	g_mutex_lock(&f->lock);
	key = g_strdup_printf("%u", ++f->last_export);
	for (i = 0; i < remote_paths->len; i++) {
		const gchar *path = g_ptr_array_index(remote_paths, i);
		const gchar *name = remmina_ftp_path_basename(path);
		gchar *local;

		if (!name[0] || g_hash_table_contains(names, name))
			continue;
		g_hash_table_insert(names, g_strdup(name), g_strdup(path));
		local = g_build_filename(f->mountpoint, key, name, NULL);
		uris[n++] = g_filename_to_uri(local, NULL, NULL);
		g_free(local);
	}
	g_hash_table_insert(f->exports, key, names);
	g_mutex_unlock(&f->lock);
	return uris;
}

void remmina_ftp_fuse_free(RemminaFTPFuse *f)
{
	if (!f)
		return;
	if (f->thread) {
		/* Connecting may need the main loop (a question): let it run until the thread is done with data */
		g_mutex_lock(&f->lock);
		while (f->connecting) {
			g_mutex_unlock(&f->lock);
			g_main_context_iteration(NULL, FALSE);
			g_usleep(10000);
			g_mutex_lock(&f->lock);
		}
		g_mutex_unlock(&f->lock);
		/* Lazily: a program still copying finishes, then the loop ends and the thread frees its share */
		if (!f->unmounted)
			fuse_unmount(f->fuse);
		f->unmounted = TRUE;
		g_thread_unref(f->thread);
		f->thread = NULL;
		ffs_mounted = g_list_remove(ffs_mounted, f);
	}
	f->data = NULL;
	ffs_unref(f);
}

/* $TMPDIR/unidesk-sftp-drag.<pid>.<n> of processes that are gone (as rdp_cliprdr.c does for FreeRDP's) */
void remmina_ftp_fuse_unmount_leftovers_at_start(void)
{
	static gboolean done = FALSE;
	const gchar *tmp = g_get_tmp_dir();
	const gchar *name;
	GDir *dir;

	if (done)
		return;
	done = TRUE;
	if (!(dir = g_dir_open(tmp, 0, NULL)))
		return;
	while ((name = g_dir_read_name(dir)) != NULL) {
		gchar *end, *path;
		GStatBuf st;
		pid_t pid;

		if (!g_str_has_prefix(name, MOUNT_PREFIX))
			continue;
		pid = (pid_t)strtol(name + strlen(MOUNT_PREFIX), &end, 10);
		if (pid <= 0 || *end != '.' || pid == getpid() || kill(pid, 0) == 0 || errno != ESRCH)
			continue;
		path = g_build_filename(tmp, name, NULL);
		/* A mount of a dead process answers ENOTCONN; leave other users' folders alone */
		if (g_lstat(path, &st) != 0 || (S_ISDIR(st.st_mode) && st.st_uid == getuid())) {
			gchar *argv[] = { (gchar *)"fusermount3", (gchar *)"-u", (gchar *)"-z", path, NULL };
			g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
				     NULL, NULL, NULL, NULL, NULL, NULL);
			g_rmdir(path);
		}
		g_free(path);
	}
	g_dir_close(dir);
}
