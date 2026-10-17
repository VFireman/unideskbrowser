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

/**
 * @file remmina_ftp_fs.h
 * @brief The same file operations for this computer and for an SFTP server.
 *
 * The two panels of the SFTP tab and the transfer thread work through these functions, so a
 * copy, a move or a deletion does not care which side a file is on. Paths are UTF-8 and
 * absolute; the remote functions convert them to the server's character set.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	REMMINA_FTP_ENTRY_FILE,
	REMMINA_FTP_ENTRY_DIR,
} RemminaFTPEntryType;

typedef struct _RemminaFTPEntry {
	gchar *			name;		/* the file name, not the path */
	RemminaFTPEntryType	type;		/* for a link: what it points to */
	gboolean		is_link;
	guint64			size;
	gint64			mtime;		/* seconds since the epoch */
	guint32			mode;		/* permission bits (07777) */
	gchar *			owner;		/* may be NULL */
	gchar *			group;		/* may be NULL */
} RemminaFTPEntry;

RemminaFTPEntry *remmina_ftp_entry_new(void);
RemminaFTPEntry *remmina_ftp_entry_copy(const RemminaFTPEntry *entry);
void remmina_ftp_entry_free(RemminaFTPEntry *entry);

typedef struct _RemminaFTPFs RemminaFTPFs;
typedef struct _RemminaFTPStream RemminaFTPStream;

RemminaFTPFs *remmina_ftp_fs_new_local(void);
/* sftp is an open RemminaSFTP; the file system only borrows it */
RemminaFTPFs *remmina_ftp_fs_new_remote(gpointer sftp);
void remmina_ftp_fs_free(RemminaFTPFs *fs);
gboolean remmina_ftp_fs_is_local(RemminaFTPFs *fs);

/* The entries of a folder, without "." and "..": a GPtrArray of RemminaFTPEntry (freed with it) */
GPtrArray *remmina_ftp_fs_list(RemminaFTPFs *fs, const gchar *dir, GError **error);
gchar *remmina_ftp_fs_realpath(RemminaFTPFs *fs, const gchar *path, GError **error);
/* lstat-like: a link stays a link (entry->is_link), entry->type tells what it points to.
 * Owner and group names are filled in too. Returns FALSE when the path does not exist. */
gboolean remmina_ftp_fs_stat(RemminaFTPFs *fs, const gchar *path, RemminaFTPEntry *entry, GError **error);
gboolean remmina_ftp_fs_mkdir(RemminaFTPFs *fs, const gchar *path, GError **error);
/* A folder must be empty; a link is removed itself, never what it points to */
gboolean remmina_ftp_fs_remove(RemminaFTPFs *fs, const gchar *path, gboolean is_dir, GError **error);
gboolean remmina_ftp_fs_rename(RemminaFTPFs *fs, const gchar *from, const gchar *to, GError **error);
gboolean remmina_ftp_fs_chmod(RemminaFTPFs *fs, const gchar *path, guint32 mode, GError **error);
gboolean remmina_ftp_fs_set_mtime(RemminaFTPFs *fs, const gchar *path, gint64 mtime, GError **error);
/* FALSE when the server cannot tell (no statvfs@openssh.com) */
gboolean remmina_ftp_fs_statvfs(RemminaFTPFs *fs, const gchar *path, guint64 *avail, guint64 *total);

RemminaFTPStream *remmina_ftp_fs_open_read(RemminaFTPFs *fs, const gchar *path, guint64 offset, GError **error);
/* append: keep what the file already has and write after it (resume) */
RemminaFTPStream *remmina_ftp_fs_open_write(RemminaFTPFs *fs, const gchar *path, gboolean append, GError **error);
/* 0 at the end of the file, -1 on error */
gssize remmina_ftp_stream_read(RemminaFTPStream *stream, gpointer buf, gsize len, GError **error);
gboolean remmina_ftp_stream_write(RemminaFTPStream *stream, gconstpointer buf, gsize len, GError **error);
void remmina_ftp_stream_close(RemminaFTPStream *stream);

gchar *remmina_ftp_path_join(const gchar *dir, const gchar *name);
gchar *remmina_ftp_path_parent(const gchar *path);
const gchar *remmina_ftp_path_basename(const gchar *path);

G_END_DECLS
