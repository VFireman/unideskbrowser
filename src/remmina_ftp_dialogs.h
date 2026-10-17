/*
 * Unidesk - dialogs of the SFTP tab, as in Total Commander
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

#pragma once

#include <gtk/gtk.h>
#include "remmina_ftp_fs.h"
#include "remmina_ftp_job.h"

G_BEGIN_DECLS

/* F5/F6: where to copy or move; the target folder (newly allocated) or NULL when cancelled */
gchar *remmina_ftp_dialog_copy(GtkWindow *parent, gboolean move, GPtrArray *targets, const gchar *dst_dir);
/* F7: the name of the new folder, or NULL */
gchar *remmina_ftp_dialog_mkdir(GtkWindow *parent);
/* F8 */
gboolean remmina_ftp_dialog_confirm_delete(GtkWindow *parent, GPtrArray *targets);
/* Num+ / Num-: a mask, "*.*" by default, or NULL */
gchar *remmina_ftp_dialog_mask(GtkWindow *parent, gboolean mark);
void remmina_ftp_dialog_message(GtkWindow *parent, const gchar *message);
/* F4: the editor saved the server file's copy; TRUE: upload it */
gboolean remmina_ftp_dialog_upload_changed(GtkWindow *parent, const gchar *name);

RemminaFTPConflict remmina_ftp_dialog_conflict(GtkWindow *parent, const gchar *src_path, const RemminaFTPEntry *src,
					       const gchar *dst_path, const RemminaFTPEntry *dst, gchar **new_name);
RemminaFTPErrorAnswer remmina_ftp_dialog_error(GtkWindow *parent, const gchar *message);

typedef struct {
	guint32		mode_set;
	guint32		mode_clear;
	gboolean	set_mtime;
	gint64		mtime;
	gboolean	recursive;
} RemminaFTPAttributes;

/* Alt+A: targets are RemminaFTPEntry (owner and group of the first one are shown). FALSE when cancelled. */
gboolean remmina_ftp_dialog_attributes(GtkWindow *parent, GPtrArray *targets, RemminaFTPAttributes *result);

/* The window of a running job */
typedef struct _RemminaFTPProgress RemminaFTPProgress;
/* on_background: the user sent the job to the background (the window is hidden by then) */
RemminaFTPProgress *remmina_ftp_progress_new(GtkWindow *parent, RemminaFTPJob *job, GCallback on_background, gpointer data);
void remmina_ftp_progress_update(RemminaFTPProgress *progress);
void remmina_ftp_progress_present(RemminaFTPProgress *progress);
GtkWindow *remmina_ftp_progress_get_window(RemminaFTPProgress *progress);
gboolean remmina_ftp_progress_is_visible(RemminaFTPProgress *progress);
void remmina_ftp_progress_free(RemminaFTPProgress *progress);

/* "Copying", "Moving"… */
const gchar *remmina_ftp_job_title(RemminaFTPJob *job);
/* "12.3 MB/s", "00:01:23" left, the percentage done (0…100) */
void remmina_ftp_job_describe(RemminaFTPJob *job, gint *percent, gchar **speed, gchar **left);

G_END_DECLS
