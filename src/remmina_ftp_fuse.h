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

/**
 * @file remmina_ftp_fuse.h
 * @brief Server files dragged out of the SFTP tab become ordinary paths for the program they land in.
 *
 * A read-only FUSE file system on $TMPDIR/unidesk-sftp-drag.<pid>.<n> shows each drag as a folder
 * "<k>" holding the dragged files and folders under their own names. The program that receives the
 * drop (Dolphin, Nautilus…) gets file:// URIs there and copies them itself; the data is read from the
 * server, while it copies, through an SFTP session of the FUSE thread. A file manager on Wayland waits
 * about a second for the drop data, so the files cannot be downloaded before the drop; and XDS does
 * not exist on Wayland.
 */

#pragma once

#include <glib.h>
#include "remmina_ftp_fs.h"

G_BEGIN_DECLS

typedef struct _RemminaFTPFuse RemminaFTPFuse;

/* Called in the FUSE thread: a file system of the server for that thread, and what to free with it;
 * NULL with *error set when the server cannot be reached */
typedef RemminaFTPFs *(*RemminaFTPFuseOpenFs)(gpointer data, gpointer *session, gchar **error);
/* Called in the FUSE thread at its end; must not use data, which may be gone by then */
typedef void (*RemminaFTPFuseCloseFs)(RemminaFTPFs *fs, gpointer session);

RemminaFTPFuse *remmina_ftp_fuse_new(RemminaFTPFuseOpenFs open_fs, RemminaFTPFuseCloseFs close_fs, gpointer data);
/* Mounts and starts the thread (it connects at once); does nothing when it runs already */
gboolean remmina_ftp_fuse_start(RemminaFTPFuse *fuse, GError **error);
/* Lends these absolute server paths: file:// URIs in the mount (NULL-terminated), NULL on error */
gchar **remmina_ftp_fuse_export(RemminaFTPFuse *fuse, GPtrArray *remote_paths, GError **error);
/* Unmounts (lazily: a copy in progress finishes) and lets go; data is not used after this returns */
void remmina_ftp_fuse_free(RemminaFTPFuse *fuse);

/* Mounts left by a process that was killed: unmount them (once, at start) */
void remmina_ftp_fuse_unmount_leftovers_at_start(void);

G_END_DECLS
