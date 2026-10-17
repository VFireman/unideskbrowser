/*
 * Unidesk - one file panel of the SFTP tab
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
 * @file remmina_ftp_panel.h
 * @brief A file panel as in Total Commander, for this computer or for the server.
 *
 * From top to bottom: whose files these are, the root and its free space, the path, the list (Name | Type | Size |
 * Date | Rights, ".." first, folders first), the totals of the marked entries. The row under
 * the cursor is the GTK selection; marked entries (Insert, Space, Num+, Ctrl+A…) are a column
 * of the model and show in red. Operations act on the marked entries, or on the cursor row.
 */

#pragma once

#include <gtk/gtk.h>
#include "remmina_ftp_fs.h"

G_BEGIN_DECLS

typedef struct _RemminaFTPPanel RemminaFTPPanel;

typedef struct {
	/* The panel has the keyboard: it is the active one */
	void (*focused)(RemminaFTPPanel *panel, gpointer data);
	/* A key the panel leaves to its owner (Tab, function keys, Alt+A, Ctrl+H): TRUE when used */
	gboolean (*key)(RemminaFTPPanel *panel, GdkEventKey *event, gpointer data);
	/* Local files dropped (file:// URIs), or the other panel's server files (uris NULL) */
	void (*dropped)(RemminaFTPPanel *panel, gchar **uris, GdkDragAction action, gpointer data);
	/* A name typed over an entry (F2) */
	void (*renamed)(RemminaFTPPanel *panel, const gchar *old_name, const gchar *new_name, gpointer data);
	/* The mask of Num+ / Num-, newly allocated, NULL when cancelled */
	gchar *(*ask_mask)(RemminaFTPPanel *panel, gboolean mark, gpointer data);
	void (*error)(RemminaFTPPanel *panel, const gchar *message, gpointer data);
	/* Server panel: its files dragged out to another program; file:// URIs (newly allocated) or NULL */
	gchar **(*export_uris)(RemminaFTPPanel *panel, GPtrArray *targets, gpointer data);
	/* Server panel: a drag begins */
	void (*drag_begin)(RemminaFTPPanel *panel, gpointer data);
	gpointer data;
} RemminaFTPPanelCallbacks;

RemminaFTPPanel *remmina_ftp_panel_new(gboolean local, const RemminaFTPPanelCallbacks *callbacks);
/* The panel lives as long as its widget */
GtkWidget *remmina_ftp_panel_get_widget(RemminaFTPPanel *panel);

gboolean remmina_ftp_panel_is_local(RemminaFTPPanel *panel);
/* NULL while the server is not connected; the panel does not own the file system */
void remmina_ftp_panel_set_fs(RemminaFTPPanel *panel, RemminaFTPFs *fs);
RemminaFTPFs *remmina_ftp_panel_get_fs(RemminaFTPPanel *panel);

/* Opens a folder (absolute, or relative to the current one); errors go to callbacks->error */
gboolean remmina_ftp_panel_open(RemminaFTPPanel *panel, const gchar *dir);
/* Reads the current folder again, keeping the cursor and the marks */
void remmina_ftp_panel_reload(RemminaFTPPanel *panel);
const gchar *remmina_ftp_panel_get_dir(RemminaFTPPanel *panel);

/* What an operation acts on: copies of the marked entries, or of the cursor entry; NULL when
 * there is none (only ".."). A GPtrArray of RemminaFTPEntry. */
GPtrArray *remmina_ftp_panel_get_targets(RemminaFTPPanel *panel);
/* The entry under the cursor (F3, F4), NULL on ".." or when there is none */
RemminaFTPEntry *remmina_ftp_panel_get_cursor_entry(RemminaFTPPanel *panel);
void remmina_ftp_panel_unmark_all(RemminaFTPPanel *panel);
/* Put the cursor on an entry of the current folder */
void remmina_ftp_panel_select_name(RemminaFTPPanel *panel, const gchar *name);

/* The line on top: whose files these are ("Local computer", "SFTP: 192.0.2.10") */
void remmina_ftp_panel_set_title(RemminaFTPPanel *panel, const gchar *title);
void remmina_ftp_panel_set_show_hidden(RemminaFTPPanel *panel, gboolean show_hidden);
gboolean remmina_ftp_panel_get_show_hidden(RemminaFTPPanel *panel);
void remmina_ftp_panel_set_active(RemminaFTPPanel *panel, gboolean active);
void remmina_ftp_panel_grab_focus(RemminaFTPPanel *panel);
/* Shown instead of the totals, NULL to show the totals again ("Connecting…") */
void remmina_ftp_panel_set_message(RemminaFTPPanel *panel, const gchar *message);
/* F2: edit the name of the cursor entry in place */
void remmina_ftp_panel_start_rename(RemminaFTPPanel *panel);

/* "11 164" with a space every three digits, for sizes in KB as Total Commander shows them */
gchar *remmina_ftp_format_kb(guint64 bytes);

G_END_DECLS
