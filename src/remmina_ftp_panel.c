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

#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <string.h>

#include "remmina_ftp_panel.h"

enum {
	COL_NAME,	/* the real name */
	COL_DISPLAY,	/* what the Name column shows: files without their extension */
	COL_EXT,
	COL_KIND,
	COL_IS_LINK,
	COL_SIZE,
	COL_MTIME,
	COL_MODE,
	COL_MARKED,
	COL_ICON,
	COL_COLLATE,
	N_COLS
};

enum { KIND_UP, KIND_DIR, KIND_FILE };
enum { SORT_NAME, SORT_EXT, SORT_SIZE, SORT_DATE, SORT_MODE };
enum { VIEW_COL_NAME, VIEW_COL_EXT, VIEW_COL_SIZE, VIEW_COL_DATE, VIEW_COL_MODE };

#define FM_TARGET_URIS		"text/uri-list"
#define FM_TARGET_REMOTE	"application/x-unidesk-fm-remote"
#define FM_QUICK_TIMEOUT	(1500 * 1000)	/* µs between the letters of a quick search */

struct _RemminaFTPPanel {
	gboolean		local;
	RemminaFTPPanelCallbacks cb;
	RemminaFTPFs *		fs;
	gchar *			dir;
	GPtrArray *		entries;	/* the last listing of dir */
	gboolean		show_hidden;
	gboolean		active;
	gchar *			message;

	GtkWidget *		box;
	GtkWidget *		title_label;
	GtkWidget *		space_label;
	GtkWidget *		path_entry;
	GtkWidget *		view;
	GtkListStore *		store;
	GtkTreeViewColumn *	name_column;
	GtkCellRenderer *	name_renderer;
	GtkCellRenderer *	size_renderer;
	GPtrArray *		renderers;	/* every cell renderer: one row height for all */
	GtkWidget *		status_label;

	gchar *			rename_from;
	GString *		quick;
	gint64			quick_time;
	GHashTable *		icons;		/* extension → GIcon */
	GIcon *			icon_dir;
	GIcon *			icon_up;

	GdkRGBA			c_bg;
	GdkRGBA			c_alt_bg;
	GdkRGBA			c_mark;
	GdkRGBA			c_mark_cursor;
};

static void panel_update_status(RemminaFTPPanel *panel);

/* ---- Formatting ---- */

/* "1 234 567" */
static gchar *panel_group_digits(guint64 value)
{
	gchar *digits = g_strdup_printf("%" G_GUINT64_FORMAT, value);
	GString *out = g_string_new(NULL);
	gsize len = strlen(digits);
	gsize i;

	for (i = 0; i < len; i++) {
		if (i > 0 && (len - i) % 3 == 0)
			g_string_append_c(out, ' ');
		g_string_append_c(out, digits[i]);
	}
	g_free(digits);
	return g_string_free(out, FALSE);
}

gchar *remmina_ftp_format_kb(guint64 bytes)
{
	return panel_group_digits(bytes / 1024);
}

static gchar *panel_format_mode(guint32 mode)
{
	gchar s[10];

	s[0] = (mode & 0400) ? 'r' : '-';
	s[1] = (mode & 0200) ? 'w' : '-';
	s[2] = (mode & 04000) ? ((mode & 0100) ? 's' : 'S') : ((mode & 0100) ? 'x' : '-');
	s[3] = (mode & 040) ? 'r' : '-';
	s[4] = (mode & 020) ? 'w' : '-';
	s[5] = (mode & 02000) ? ((mode & 010) ? 's' : 'S') : ((mode & 010) ? 'x' : '-');
	s[6] = (mode & 04) ? 'r' : '-';
	s[7] = (mode & 02) ? 'w' : '-';
	s[8] = (mode & 01000) ? ((mode & 01) ? 't' : 'T') : ((mode & 01) ? 'x' : '-');
	s[9] = '\0';
	return g_strdup(s);
}

static gchar *panel_format_date(gint64 mtime)
{
	GDateTime *dt = g_date_time_new_from_unix_local(mtime);
	gchar *str;

	if (!dt)
		return g_strdup("");
	str = g_date_time_format(dt, "%d.%m.%Y %H:%M");
	g_date_time_unref(dt);
	return str;
}

/* "report.tar.gz" shows as "report.tar" | "gz", like Total Commander; ".bashrc" has no extension */
static void panel_split_name(const gchar *name, gboolean is_file, gchar **display, gchar **ext)
{
	const gchar *dot = is_file ? strrchr(name, '.') : NULL;

	if (dot && dot != name && dot[1]) {
		*display = g_strndup(name, (gsize)(dot - name));
		*ext = g_strdup(dot + 1);
	} else {
		*display = g_strdup(name);
		*ext = g_strdup("");
	}
}

/* ---- Colours: named in data/style/unidesk-fm.css ---- */

static void panel_lookup_color(GtkStyleContext *ctx, const gchar *name, const gchar *fallback, GdkRGBA *color)
{
	if (!gtk_style_context_lookup_color(ctx, name, color))
		gdk_rgba_parse(color, fallback);
}

static void panel_on_style_updated(GtkWidget *widget, RemminaFTPPanel *panel)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
	PangoContext *pango = gtk_widget_get_pango_context(widget);
	PangoFontMetrics *metrics;
	gint height;
	guint i;

	panel_lookup_color(ctx, "tc_bg", "#000000", &panel->c_bg);
	panel_lookup_color(ctx, "tc_alt_bg", "#0f0f0f", &panel->c_alt_bg);
	panel_lookup_color(ctx, "tc_mark_fg", "#ff0000", &panel->c_mark);
	panel_lookup_color(ctx, "tc_mark_cursor_fg", "#ffb0b0", &panel->c_mark_cursor);

	/* Dense rows as in Total Commander: the height of a line of text (or of a 16 px icon) */
	metrics = pango_context_get_metrics(pango, pango_context_get_font_description(pango), NULL);
	height = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics));
	pango_font_metrics_unref(metrics);
	height = MAX(height, 16) + 2;
	for (i = 0; panel->renderers && i < panel->renderers->len; i++)
		gtk_cell_renderer_set_fixed_size(g_ptr_array_index(panel->renderers, i), -1, height);
}

/* ---- Cells ---- */

static void panel_cell_colors(RemminaFTPPanel *panel, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
			      gboolean text)
{
	GtkTreePath *path = gtk_tree_model_get_path(model, iter);
	gint index = gtk_tree_path_get_indices(path)[0];
	gboolean cursor = panel->active &&
			  gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), path);
	gboolean marked;

	gtk_tree_model_get(model, iter, COL_MARKED, &marked, -1);
	gtk_tree_path_free(path);

	/* The cursor row keeps the selection colour of the theme; the others alternate */
	if (cursor)
		g_object_set(renderer, "cell-background-set", FALSE, NULL);
	else
		g_object_set(renderer, "cell-background-rgba", (index % 2) ? &panel->c_alt_bg : &panel->c_bg, NULL);

	if (!text)
		return;
	if (marked)
		g_object_set(renderer, "foreground-rgba", cursor ? &panel->c_mark_cursor : &panel->c_mark, NULL);
	else
		g_object_set(renderer, "foreground-set", FALSE, NULL);
}

static void panel_cell_icon(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
			    gpointer data)
{
	GIcon *icon;

	gtk_tree_model_get(model, iter, COL_ICON, &icon, -1);
	g_object_set(renderer, "gicon", icon, NULL);
	if (icon)
		g_object_unref(icon);
	panel_cell_colors(data, renderer, model, iter, FALSE);
}

static void panel_cell_text(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
			    gpointer data)
{
	gint which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), "fm-column"));
	gchar *text = NULL;
	gint kind;
	guint64 size;
	gint64 mtime;
	guint mode;

	gtk_tree_model_get(model, iter, COL_KIND, &kind, COL_SIZE, &size, COL_MTIME, &mtime, COL_MODE, &mode, -1);

	switch (which) {
	case VIEW_COL_NAME: {
		gchar *display;
		gtk_tree_model_get(model, iter, COL_DISPLAY, &display, -1);
		/* Names are bytes on Linux: show what does not decode as UTF-8 escaped */
		text = g_utf8_validate(display, -1, NULL) ? g_strdup(display) : g_filename_display_name(display);
		g_free(display);
		break;
	}
	case VIEW_COL_EXT:
		gtk_tree_model_get(model, iter, COL_EXT, &text, -1);
		break;
	case VIEW_COL_SIZE:
		if (kind == KIND_FILE) {
			/* In bytes, as Total Commander shows it by default: the totals line has the kilobytes */
			text = panel_group_digits(size);
		} else {
			/* TRANSLATORS: the Size column of a folder, as in Total Commander */
			text = g_strdup(_("<DIR>"));
		}
		break;
	case VIEW_COL_DATE:
		text = kind == KIND_UP ? g_strdup("") : panel_format_date(mtime);
		break;
	case VIEW_COL_MODE:
		text = kind == KIND_UP ? g_strdup("") : panel_format_mode(mode);
		break;
	}
	g_object_set(renderer, "text", text ? text : "", NULL);
	g_free(text);
	panel_cell_colors(data, renderer, model, iter, TRUE);
}

/* ".." first, then folders, then files, whatever the direction of the sort */
static gint panel_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint sort_id = GPOINTER_TO_INT(data);
	gint current_id;
	GtkSortType order;
	gint ka, kb, ret = 0;
	gchar *ca, *cb;

	gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &current_id, &order);
	gtk_tree_model_get(model, a, COL_KIND, &ka, -1);
	gtk_tree_model_get(model, b, COL_KIND, &kb, -1);
	if (ka != kb)
		return (ka - kb) * (order == GTK_SORT_DESCENDING ? -1 : 1);

	switch (sort_id) {
	case SORT_EXT:
		if (ka == KIND_FILE) {
			gchar *ea, *eb;
			gtk_tree_model_get(model, a, COL_EXT, &ea, -1);
			gtk_tree_model_get(model, b, COL_EXT, &eb, -1);
			ret = g_ascii_strcasecmp(ea, eb);
			g_free(ea);
			g_free(eb);
		}
		break;
	case SORT_SIZE:
		if (ka == KIND_FILE) {
			guint64 sa, sb;
			gtk_tree_model_get(model, a, COL_SIZE, &sa, -1);
			gtk_tree_model_get(model, b, COL_SIZE, &sb, -1);
			ret = sa < sb ? -1 : (sa > sb ? 1 : 0);
		}
		break;
	case SORT_DATE: {
		gint64 ma, mb;
		gtk_tree_model_get(model, a, COL_MTIME, &ma, -1);
		gtk_tree_model_get(model, b, COL_MTIME, &mb, -1);
		ret = ma < mb ? -1 : (ma > mb ? 1 : 0);
		break;
	}
	case SORT_MODE: {
		guint ma, mb;
		gtk_tree_model_get(model, a, COL_MODE, &ma, -1);
		gtk_tree_model_get(model, b, COL_MODE, &mb, -1);
		ret = (gint)ma - (gint)mb;
		break;
	}
	default:
		break;
	}
	if (ret != 0)
		return ret;

	gtk_tree_model_get(model, a, COL_COLLATE, &ca, -1);
	gtk_tree_model_get(model, b, COL_COLLATE, &cb, -1);
	ret = g_strcmp0(ca, cb);
	g_free(ca);
	g_free(cb);
	return ret;
}

/* ---- Filling the list ---- */

static GIcon *panel_icon_for(RemminaFTPPanel *panel, gint kind, const gchar *name, const gchar *ext)
{
	GIcon *icon;
	gchar *type;

	if (kind == KIND_UP)
		return panel->icon_up;
	if (kind == KIND_DIR)
		return panel->icon_dir;
	icon = g_hash_table_lookup(panel->icons, ext);
	if (icon)
		return icon;
	type = g_content_type_guess(name, NULL, 0, NULL);
	icon = g_content_type_get_icon(type);
	g_free(type);
	g_hash_table_insert(panel->icons, g_strdup(ext), icon);
	return icon;
}

static void panel_append(RemminaFTPPanel *panel, const RemminaFTPEntry *entry, gboolean marked)
{
	GtkTreeIter iter;
	gint kind = entry->type == REMMINA_FTP_ENTRY_DIR ? KIND_DIR : KIND_FILE;
	gchar *display, *ext, *folded, *collate;

	panel_split_name(entry->name, kind == KIND_FILE, &display, &ext);
	folded = g_utf8_validate(entry->name, -1, NULL) ? g_utf8_casefold(entry->name, -1) : g_strdup(entry->name);
	collate = g_utf8_validate(folded, -1, NULL) ? g_utf8_collate_key_for_filename(folded, -1) : g_strdup(folded);
	gtk_list_store_insert_with_values(panel->store, &iter, -1,
					  COL_NAME, entry->name,
					  COL_DISPLAY, display,
					  COL_EXT, ext,
					  COL_KIND, kind,
					  COL_IS_LINK, entry->is_link,
					  COL_SIZE, entry->size,
					  COL_MTIME, entry->mtime,
					  COL_MODE, entry->mode,
					  COL_MARKED, marked,
					  COL_ICON, panel_icon_for(panel, kind, entry->name, ext),
					  COL_COLLATE, collate,
					  -1);
	g_free(display);
	g_free(ext);
	g_free(folded);
	g_free(collate);
}

static gboolean panel_find_name(RemminaFTPPanel *panel, const gchar *name, GtkTreeIter *iter)
{
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	gboolean valid;

	for (valid = gtk_tree_model_get_iter_first(model, iter); valid; valid = gtk_tree_model_iter_next(model, iter)) {
		gchar *n;
		gboolean same;
		gtk_tree_model_get(model, iter, COL_NAME, &n, -1);
		same = g_strcmp0(n, name) == 0;
		g_free(n);
		if (same)
			return TRUE;
	}
	return FALSE;
}

static void panel_set_cursor_iter(RemminaFTPPanel *panel, GtkTreeIter *iter)
{
	GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(panel->store), iter);

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(panel->view), path, NULL, FALSE);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(panel->view), path, NULL, FALSE, 0, 0);
	gtk_tree_path_free(path);
}

static void panel_fill(RemminaFTPPanel *panel, GHashTable *marks, const gchar *cursor_name)
{
	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(panel->store);
	GtkTreeIter iter;
	gint sort_id;
	GtkSortType order;
	guint64 biggest = 0;
	gchar *digits;
	guint i;

	/* A new folder starts a new quick search */
	g_string_truncate(panel->quick, 0);
	gtk_tree_sortable_get_sort_column_id(sortable, &sort_id, &order);
	gtk_tree_sortable_set_sort_column_id(sortable, GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);
	gtk_list_store_clear(panel->store);

	if (panel->dir && g_strcmp0(panel->dir, "/") != 0) {
		gtk_list_store_insert_with_values(panel->store, &iter, -1,
						  COL_NAME, "..", COL_DISPLAY, "..", COL_EXT, "", COL_KIND, KIND_UP,
						  COL_IS_LINK, FALSE, COL_SIZE, (guint64)0, COL_MTIME, (gint64)0, COL_MODE, 0,
						  COL_MARKED, FALSE, COL_ICON, panel->icon_up, COL_COLLATE, "", -1);
	}
	for (i = 0; panel->entries && i < panel->entries->len; i++) {
		RemminaFTPEntry *entry = g_ptr_array_index(panel->entries, i);

		if (!panel->show_hidden && entry->name[0] == '.')
			continue;
		panel_append(panel, entry, marks && g_hash_table_contains(marks, entry->name));
		if (entry->type == REMMINA_FTP_ENTRY_FILE)
			biggest = MAX(biggest, entry->size);
	}
	/* The Size column as wide as the biggest number of this folder: narrow panels keep more of the names */
	digits = panel_group_digits(biggest);
	g_object_set(panel->size_renderer, "width-chars",
		     (gint)MAX(strlen(digits), (gsize)g_utf8_strlen(_("<DIR>"), -1)) + 1, NULL);
	g_free(digits);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(panel->view));

	gtk_tree_sortable_set_sort_column_id(sortable, sort_id, order);

	if (cursor_name && panel_find_name(panel, cursor_name, &iter))
		panel_set_cursor_iter(panel, &iter);
	else if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(panel->store), &iter))
		panel_set_cursor_iter(panel, &iter);
	panel_update_status(panel);
}

static gchar *panel_cursor_name(RemminaFTPPanel *panel, gint *kind)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *name = NULL;

	if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), &model, &iter))
		return NULL;
	gtk_tree_model_get(model, &iter, COL_NAME, &name, COL_KIND, kind, -1);
	return name;
}

static GHashTable *panel_marked_names(RemminaFTPPanel *panel)
{
	GHashTable *marks = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;

	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gboolean marked;
		gchar *name;
		gtk_tree_model_get(model, &iter, COL_MARKED, &marked, COL_NAME, &name, -1);
		if (marked)
			g_hash_table_add(marks, name);
		else
			g_free(name);
	}
	return marks;
}

/* ---- Lines around the list ---- */

static void panel_update_path(RemminaFTPPanel *panel)
{
	gchar *text;

	if (!panel->dir) {
		gtk_entry_set_text(GTK_ENTRY(panel->path_entry), "");
		return;
	}
	/* Total Commander shows the folder with its file mask */
	text = g_str_has_suffix(panel->dir, "/") ? g_strconcat(panel->dir, "*.*", NULL) :
	       g_strconcat(panel->dir, "/*.*", NULL);
	gtk_entry_set_text(GTK_ENTRY(panel->path_entry), text);
	g_free(text);
}

static void panel_update_space(RemminaFTPPanel *panel)
{
	guint64 avail = 0, total = 0;
	gchar *a, *t, *text;

	if (!panel->fs || !panel->dir || !remmina_ftp_fs_statvfs(panel->fs, panel->dir, &avail, &total)) {
		gtk_label_set_text(GTK_LABEL(panel->space_label), "");
		return;
	}
	a = remmina_ftp_format_kb(avail);
	t = remmina_ftp_format_kb(total);
	/* TRANSLATORS: the free space of the disk above a panel, as in Total Commander */
	text = g_strdup_printf(_("%s KB of %s KB free"), a, t);
	gtk_label_set_text(GTK_LABEL(panel->space_label), text);
	g_free(a);
	g_free(t);
	g_free(text);
}

static void panel_update_status(RemminaFTPPanel *panel)
{
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;
	guint64 size_all = 0, size_marked = 0;
	guint files = 0, files_marked = 0, dirs = 0, dirs_marked = 0;
	gchar *s1, *s2, *text;

	if (panel->message) {
		gtk_label_set_text(GTK_LABEL(panel->status_label), panel->message);
		return;
	}
	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gint kind;
		gboolean marked;
		guint64 size;

		gtk_tree_model_get(model, &iter, COL_KIND, &kind, COL_MARKED, &marked, COL_SIZE, &size, -1);
		if (kind == KIND_FILE) {
			files++;
			size_all += size;
			if (marked) {
				files_marked++;
				size_marked += size;
			}
		} else if (kind == KIND_DIR) {
			dirs++;
			if (marked)
				dirs_marked++;
		}
	}
	s1 = remmina_ftp_format_kb(size_marked);
	s2 = remmina_ftp_format_kb(size_all);
	/* TRANSLATORS: the line under a panel, as in Total Commander: marked of all */
	text = g_strdup_printf(_("%s KB of %s KB, files: %u of %u, folders: %u of %u"),
			       s1, s2, files_marked, files, dirs_marked, dirs);
	gtk_label_set_text(GTK_LABEL(panel->status_label), text);
	g_free(s1);
	g_free(s2);
	g_free(text);
}

static void panel_report_error(RemminaFTPPanel *panel, GError *error)
{
	if (panel->cb.error)
		panel->cb.error(panel, error ? error->message : _("Unknown error"), panel->cb.data);
}

/* ---- Navigation ---- */

gboolean remmina_ftp_panel_open(RemminaFTPPanel *panel, const gchar *dir)
{
	GError *error = NULL;
	gchar *target, *real;
	GPtrArray *entries;
	gchar *came_from = NULL;

	if (!panel->fs)
		return FALSE;
	if (!dir || !dir[0])
		dir = "/";
	target = dir[0] == '/' ? g_strdup(dir) : remmina_ftp_path_join(panel->dir ? panel->dir : "/", dir);
	real = remmina_ftp_fs_realpath(panel->fs, target, &error);
	g_free(target);
	if (!real) {
		panel_report_error(panel, error);
		g_clear_error(&error);
		return FALSE;
	}
	entries = remmina_ftp_fs_list(panel->fs, real, &error);
	if (!entries) {
		panel_report_error(panel, error);
		g_clear_error(&error);
		g_free(real);
		return FALSE;
	}

	/* Going up: the cursor lands on the folder we came from */
	if (panel->dir) {
		gsize len = strlen(real);
		if (g_str_has_prefix(panel->dir, real) && strlen(panel->dir) > len &&
		    (panel->dir[len] == '/' || g_strcmp0(real, "/") == 0)) {
			const gchar *rest = panel->dir + len + (panel->dir[len] == '/' ? 1 : 0);
			const gchar *slash = strchr(rest, '/');
			came_from = slash ? g_strndup(rest, (gsize)(slash - rest)) : g_strdup(rest);
		}
	}

	g_free(panel->dir);
	panel->dir = real;
	if (panel->entries)
		g_ptr_array_unref(panel->entries);
	panel->entries = entries;
	panel_fill(panel, NULL, came_from);
	g_free(came_from);
	panel_update_path(panel);
	panel_update_space(panel);
	return TRUE;
}

void remmina_ftp_panel_reload(RemminaFTPPanel *panel)
{
	GError *error = NULL;
	GPtrArray *entries;
	GHashTable *marks;
	gchar *cursor;
	gint kind;

	if (!panel->fs || !panel->dir)
		return;
	entries = remmina_ftp_fs_list(panel->fs, panel->dir, &error);
	if (!entries) {
		/* The folder is gone: go up until something opens */
		gchar *parent = remmina_ftp_path_parent(panel->dir);
		g_clear_error(&error);
		while (!remmina_ftp_panel_open(panel, parent) && g_strcmp0(parent, "/") != 0) {
			gchar *up = remmina_ftp_path_parent(parent);
			g_free(parent);
			parent = up;
		}
		g_free(parent);
		return;
	}
	marks = panel_marked_names(panel);
	cursor = panel_cursor_name(panel, &kind);
	if (panel->entries)
		g_ptr_array_unref(panel->entries);
	panel->entries = entries;
	panel_fill(panel, marks, cursor);
	g_hash_table_unref(marks);
	g_free(cursor);
	panel_update_space(panel);
}

static void panel_go_up(RemminaFTPPanel *panel)
{
	if (panel->dir && g_strcmp0(panel->dir, "/") != 0)
		remmina_ftp_panel_open(panel, "..");
}

static void panel_activate_cursor(RemminaFTPPanel *panel)
{
	gint kind = KIND_FILE;
	gchar *name = panel_cursor_name(panel, &kind);

	if (name && kind == KIND_UP)
		panel_go_up(panel);
	else if (name && kind == KIND_DIR)
		remmina_ftp_panel_open(panel, name);
	/* Enter on a file does nothing */
	g_free(name);
}

/* ---- Marks ---- */

static void panel_toggle_mark_iter(RemminaFTPPanel *panel, GtkTreeIter *iter)
{
	gint kind;
	gboolean marked;

	gtk_tree_model_get(GTK_TREE_MODEL(panel->store), iter, COL_KIND, &kind, COL_MARKED, &marked, -1);
	if (kind != KIND_UP)
		gtk_list_store_set(panel->store, iter, COL_MARKED, !marked, -1);
}

static void panel_toggle_mark_and_move(RemminaFTPPanel *panel)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), &model, &iter))
		return;
	panel_toggle_mark_iter(panel, &iter);
	if (gtk_tree_model_iter_next(model, &iter))
		panel_set_cursor_iter(panel, &iter);
	panel_update_status(panel);
}

/* Total Commander masks: "*.txt;*.log", "*.*" matches names without a dot too */
static gboolean panel_mask_match(GPtrArray *patterns, const gchar *name)
{
	gchar *lower = g_utf8_validate(name, -1, NULL) ? g_utf8_strdown(name, -1) : g_ascii_strdown(name, -1);
	gboolean match = FALSE;
	guint i;

	for (i = 0; i < patterns->len && !match; i++)
		match = g_pattern_match_string(g_ptr_array_index(patterns, i), lower);
	g_free(lower);
	return match;
}

static void panel_mark_by_mask(RemminaFTPPanel *panel, gboolean mark)
{
	GPtrArray *patterns;
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;
	gchar *mask, **parts;
	guint i;

	if (!panel->cb.ask_mask)
		return;
	mask = panel->cb.ask_mask(panel, mark, panel->cb.data);
	if (!mask)
		return;
	patterns = g_ptr_array_new_with_free_func((GDestroyNotify)g_pattern_spec_free);
	parts = g_strsplit_set(mask, "; ", -1);
	for (i = 0; parts[i]; i++) {
		gchar *p;
		if (!parts[i][0])
			continue;
		p = g_utf8_strdown(g_strcmp0(parts[i], "*.*") == 0 ? "*" : parts[i], -1);
		g_ptr_array_add(patterns, g_pattern_spec_new(p));
		g_free(p);
	}
	g_strfreev(parts);
	g_free(mask);

	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gint kind;
		gchar *name;
		gtk_tree_model_get(model, &iter, COL_KIND, &kind, COL_NAME, &name, -1);
		/* Num+ marks files, Num- unmarks everything that matches */
		if ((kind == KIND_FILE || (!mark && kind == KIND_DIR)) && panel_mask_match(patterns, name))
			gtk_list_store_set(panel->store, &iter, COL_MARKED, mark, -1);
		g_free(name);
	}
	g_ptr_array_unref(patterns);
	panel_update_status(panel);
}

static void panel_mark_all(RemminaFTPPanel *panel, gint how)
{
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;

	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gint kind;
		gboolean marked;
		gtk_tree_model_get(model, &iter, COL_KIND, &kind, COL_MARKED, &marked, -1);
		if (kind == KIND_UP)
			continue;
		if (how == 1)			/* Ctrl+A: everything */
			gtk_list_store_set(panel->store, &iter, COL_MARKED, TRUE, -1);
		else if (how == 0)		/* unmark */
			gtk_list_store_set(panel->store, &iter, COL_MARKED, FALSE, -1);
		else if (kind == KIND_FILE)	/* Num*: invert the files */
			gtk_list_store_set(panel->store, &iter, COL_MARKED, !marked, -1);
	}
	panel_update_status(panel);
}

void remmina_ftp_panel_unmark_all(RemminaFTPPanel *panel)
{
	panel_mark_all(panel, 0);
}

/* ---- Keyboard and mouse ---- */

static void panel_sort(RemminaFTPPanel *panel, gint sort_id)
{
	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(panel->store);
	gint current;
	GtkSortType order;

	gtk_tree_sortable_get_sort_column_id(sortable, &current, &order);
	if (current == sort_id)
		order = order == GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
	else
		order = GTK_SORT_ASCENDING;
	gtk_tree_sortable_set_sort_column_id(sortable, sort_id, order);
}

static gboolean panel_quick_search(RemminaFTPPanel *panel, guint32 ch)
{
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;
	gint64 now = g_get_monotonic_time();
	gchar *prefix;
	gsize old_len;

	if (now - panel->quick_time > FM_QUICK_TIMEOUT)
		g_string_truncate(panel->quick, 0);
	panel->quick_time = now;
	old_len = panel->quick->len;
	g_string_append_unichar(panel->quick, ch);
	prefix = g_utf8_casefold(panel->quick->str, -1);

	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gchar *name, *folded;
		gboolean match;
		gtk_tree_model_get(model, &iter, COL_NAME, &name, -1);
		folded = g_utf8_validate(name, -1, NULL) ? g_utf8_casefold(name, -1) : g_strdup(name);
		match = g_str_has_prefix(folded, prefix) && g_strcmp0(name, "..") != 0;
		g_free(name);
		g_free(folded);
		if (match) {
			panel_set_cursor_iter(panel, &iter);
			break;
		}
	}
	if (!valid)
		g_string_truncate(panel->quick, old_len);
	g_free(prefix);
	return TRUE;
}

static gboolean panel_on_key_press(GtkWidget *widget, GdkEventKey *event, RemminaFTPPanel *panel)
{
	GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
	guint32 ch = gdk_keyval_to_unicode(event->keyval);
	gboolean letter = ch && !(mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) && g_unichar_isprint(ch) &&
			  ch != ' ';

	/* Any other key ends the quick search: Enter, arrows, Insert… */
	if (!letter && !event->is_modifier)
		g_string_truncate(panel->quick, 0);

	switch (event->keyval) {
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		if (mods == 0) {
			panel_activate_cursor(panel);
			return TRUE;
		}
		break;
	case GDK_KEY_BackSpace:
		if (mods == 0) {
			panel_go_up(panel);
			return TRUE;
		}
		break;
	case GDK_KEY_Page_Up:
		if (mods == GDK_CONTROL_MASK) {
			panel_go_up(panel);
			return TRUE;
		}
		break;
	case GDK_KEY_backslash:
		if (mods == GDK_CONTROL_MASK) {
			remmina_ftp_panel_open(panel, "/");
			return TRUE;
		}
		break;
	case GDK_KEY_Insert:
	case GDK_KEY_KP_Insert:
		if (mods == 0) {
			panel_toggle_mark_and_move(panel);
			return TRUE;
		}
		break;
	case GDK_KEY_space:
		if (mods == 0) {
			panel_toggle_mark_and_move(panel);
			return TRUE;
		}
		break;
	case GDK_KEY_KP_Add:
		panel_mark_by_mask(panel, TRUE);
		return TRUE;
	case GDK_KEY_KP_Subtract:
		panel_mark_by_mask(panel, FALSE);
		return TRUE;
	case GDK_KEY_KP_Multiply:
		panel_mark_all(panel, 2);
		return TRUE;
	case GDK_KEY_a:
	case GDK_KEY_A:
		if (mods == GDK_CONTROL_MASK) {
			panel_mark_all(panel, 1);
			return TRUE;
		}
		break;
	case GDK_KEY_F3:
	case GDK_KEY_F4:
	case GDK_KEY_F5:
	case GDK_KEY_F6:
		if (mods == GDK_CONTROL_MASK) {
			static const gint by_key[] = { SORT_NAME, SORT_EXT, SORT_DATE, SORT_SIZE };
			panel_sort(panel, by_key[event->keyval - GDK_KEY_F3]);
			return TRUE;
		}
		break;
	default:
		break;
	}

	/* Tab, function keys, Alt+A, Ctrl+H… belong to the owner */
	if (panel->cb.key && panel->cb.key(panel, event, panel->cb.data))
		return TRUE;

	/* Letters move the cursor to the first name that starts with them */
	if (letter)
		return panel_quick_search(panel, ch);

	return FALSE;
}

static gboolean panel_on_button_press(GtkWidget *widget, GdkEventButton *event, RemminaFTPPanel *panel)
{
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();

	if (event->window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget)))
		return FALSE;
	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
		return FALSE;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(panel->store), &iter, path);
	gtk_widget_grab_focus(widget);

	if (event->type == GDK_2BUTTON_PRESS && event->button == 1 && mods == 0) {
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
		gtk_tree_path_free(path);
		panel_activate_cursor(panel);
		return TRUE;
	}
	if (event->type == GDK_BUTTON_PRESS && (event->button == 3 || (event->button == 1 && mods == GDK_CONTROL_MASK))) {
		/* The right button and Ctrl+click mark, as in Total Commander */
		panel_toggle_mark_iter(panel, &iter);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
		gtk_tree_path_free(path);
		panel_update_status(panel);
		return TRUE;
	}
	if (event->type == GDK_BUTTON_PRESS && event->button == 1 && mods == GDK_SHIFT_MASK) {
		/* Shift+click marks from the cursor to here */
		GtkTreeModel *model;
		GtkTreeIter cur;
		if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), &model, &cur)) {
			GtkTreePath *from = gtk_tree_model_get_path(model, &cur);
			gint a = gtk_tree_path_get_indices(from)[0];
			gint b = gtk_tree_path_get_indices(path)[0];
			gint i;
			for (i = MIN(a, b); i <= MAX(a, b); i++) {
				GtkTreeIter it;
				gint kind;
				if (!gtk_tree_model_iter_nth_child(model, &it, NULL, i))
					continue;
				gtk_tree_model_get(model, &it, COL_KIND, &kind, -1);
				if (kind != KIND_UP)
					gtk_list_store_set(panel->store, &it, COL_MARKED, TRUE, -1);
			}
			gtk_tree_path_free(from);
		}
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
		gtk_tree_path_free(path);
		panel_update_status(panel);
		return TRUE;
	}
	gtk_tree_path_free(path);
	return FALSE;
}

static gboolean panel_on_focus_in(GtkWidget *widget, GdkEventFocus *event, RemminaFTPPanel *panel)
{
	if (panel->cb.focused)
		panel->cb.focused(panel, panel->cb.data);
	return FALSE;
}

static void panel_on_cursor_changed(GtkTreeView *view, RemminaFTPPanel *panel)
{
	/* The previous cursor row gets its alternating colour back */
	gtk_widget_queue_draw(GTK_WIDGET(view));
}

static void panel_on_path_activate(GtkEntry *entry, RemminaFTPPanel *panel)
{
	gchar *text = g_strstrip(g_strdup(gtk_entry_get_text(entry)));
	gsize len = strlen(text);

	/* The folder followed by its mask ("*.*", as shown) opens the folder */
	if (g_str_has_suffix(text, "*.*"))
		text[len - 3] = '\0';
	else if (g_str_has_suffix(text, "*"))
		text[len - 1] = '\0';
	if (!remmina_ftp_panel_open(panel, text))
		panel_update_path(panel);
	g_free(text);
	gtk_widget_grab_focus(panel->view);
}

static gboolean panel_on_path_key(GtkWidget *widget, GdkEventKey *event, RemminaFTPPanel *panel)
{
	if (event->keyval == GDK_KEY_Escape) {
		panel_update_path(panel);
		gtk_widget_grab_focus(panel->view);
		return TRUE;
	}
	return FALSE;
}

static void panel_on_root_clicked(GtkButton *button, RemminaFTPPanel *panel)
{
	remmina_ftp_panel_open(panel, "/");
	gtk_widget_grab_focus(panel->view);
}

static void panel_on_up_clicked(GtkButton *button, RemminaFTPPanel *panel)
{
	panel_go_up(panel);
	gtk_widget_grab_focus(panel->view);
}

/* ---- Renaming in place ---- */

typedef struct {
	GtkEditable *	editable;
	gint		end;
} PanelSelection;

static gboolean panel_select_later(gpointer data)
{
	PanelSelection *sel = data;

	gtk_editable_select_region(sel->editable, 0, sel->end);
	g_object_unref(sel->editable);
	g_free(sel);
	return G_SOURCE_REMOVE;
}

static void panel_on_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, RemminaFTPPanel *panel)
{
	PanelSelection *sel;
	const gchar *dot;

	if (!GTK_IS_ENTRY(editable) || !panel->rename_from)
		return;
	/* The Name column hides the extension: edit the whole name, with the part before it selected
	 * (once the entry has the focus: taking it selects everything) */
	gtk_entry_set_text(GTK_ENTRY(editable), panel->rename_from);
	dot = strrchr(panel->rename_from, '.');
	sel = g_new0(PanelSelection, 1);
	sel->editable = GTK_EDITABLE(g_object_ref(editable));
	sel->end = dot && dot != panel->rename_from && g_utf8_validate(panel->rename_from, -1, NULL) ?
		   (gint)g_utf8_pointer_to_offset(panel->rename_from, dot) : -1;
	gtk_editable_select_region(sel->editable, 0, sel->end);
	g_idle_add(panel_select_later, sel);
}

static void panel_end_rename(RemminaFTPPanel *panel)
{
	g_object_set(panel->name_renderer, "editable", FALSE, NULL);
	g_free(panel->rename_from);
	panel->rename_from = NULL;
	gtk_widget_grab_focus(panel->view);
}

static void panel_on_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, RemminaFTPPanel *panel)
{
	gchar *from = g_strdup(panel->rename_from);

	panel_end_rename(panel);
	if (from && new_text && new_text[0] && g_strcmp0(from, new_text) != 0 && panel->cb.renamed)
		panel->cb.renamed(panel, from, new_text, panel->cb.data);
	g_free(from);
}

static void panel_on_editing_canceled(GtkCellRenderer *renderer, RemminaFTPPanel *panel)
{
	panel_end_rename(panel);
}

void remmina_ftp_panel_start_rename(RemminaFTPPanel *panel)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	gint kind;
	gchar *name;

	if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), &model, &iter))
		return;
	gtk_tree_model_get(model, &iter, COL_NAME, &name, COL_KIND, &kind, -1);
	if (kind == KIND_UP) {
		g_free(name);
		return;
	}
	g_free(panel->rename_from);
	panel->rename_from = name;
	g_object_set(panel->name_renderer, "editable", TRUE, NULL);
	path = gtk_tree_model_get_path(model, &iter);
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(panel->view), path, panel->name_column, panel->name_renderer, TRUE);
	gtk_tree_path_free(path);
}

/* ---- Drag and drop ---- */

static void panel_on_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *data, guint info,
				   guint time, RemminaFTPPanel *panel)
{
	GPtrArray *targets;
	gchar **uris;
	guint i;

	if (info == 1) {
		/* Between our panels: the owner reads the selection of the other panel itself */
		gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, (const guchar *)"1", 1);
		return;
	}
	targets = remmina_ftp_panel_get_targets(panel);
	if (!targets)
		return;
	if (!panel->local) {
		/* Server files for another program: the owner lends them as local paths */
		uris = panel->cb.export_uris ? panel->cb.export_uris(panel, targets, panel->cb.data) : NULL;
	} else {
		uris = g_new0(gchar *, targets->len + 1);
		for (i = 0; i < targets->len; i++) {
			RemminaFTPEntry *entry = g_ptr_array_index(targets, i);
			gchar *path = remmina_ftp_path_join(panel->dir, entry->name);
			uris[i] = g_filename_to_uri(path, NULL, NULL);
			g_free(path);
		}
	}
	if (uris)
		gtk_selection_data_set_uris(data, uris);
	g_strfreev(uris);
	g_ptr_array_unref(targets);
}

static void panel_on_drag_begin(GtkWidget *widget, GdkDragContext *context, RemminaFTPPanel *panel)
{
	if (!panel->local && panel->cb.drag_begin)
		panel->cb.drag_begin(panel, panel->cb.data);
}

/* GtkListStore would delete the dragged rows after a move */
static void panel_on_drag_data_delete(GtkWidget *widget, GdkDragContext *context, RemminaFTPPanel *panel)
{
	g_signal_stop_emission_by_name(widget, "drag-data-delete");
}

static gboolean panel_on_drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time,
				   RemminaFTPPanel *panel)
{
	GdkAtom target = gtk_drag_dest_find_target(widget, context, NULL);

	if (target == GDK_NONE || gtk_drag_get_source_widget(context) == panel->view)
		return FALSE;
	gtk_drag_get_data(widget, context, target, time);
	return TRUE;
}

static void panel_on_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data,
					guint info, guint time, RemminaFTPPanel *panel)
{
	GtkWidget *source = gtk_drag_get_source_widget(context);
	/* Only our own panels may move: an outside file manager would delete its files itself */
	GdkDragAction action = source ? gdk_drag_context_get_selected_action(context) : GDK_ACTION_COPY;

	if (source == panel->view || !panel->cb.dropped) {
		gtk_drag_finish(context, FALSE, FALSE, time);
		return;
	}
	if (info == 1) {
		panel->cb.dropped(panel, NULL, action, panel->cb.data);
	} else {
		gchar **uris = gtk_selection_data_get_uris(data);
		if (uris)
			panel->cb.dropped(panel, uris, action, panel->cb.data);
		g_strfreev(uris);
	}
	gtk_drag_finish(context, TRUE, FALSE, time);
}

/* ---- Building ---- */

static void panel_add_column(RemminaFTPPanel *panel, gint which, const gchar *title, gint sort_id, gfloat xalign,
			     gint width_chars)
{
	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	GtkCellRenderer *renderer;

	gtk_tree_view_column_set_title(col, title);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_sort_column_id(col, sort_id);
	g_object_set_data(G_OBJECT(col), "fm-column", GINT_TO_POINTER(which));

	if (which == VIEW_COL_NAME) {
		renderer = gtk_cell_renderer_pixbuf_new();
		g_object_set(renderer, "stock-size", GTK_ICON_SIZE_MENU, "xpad", 2, "ypad", 0, NULL);
		gtk_tree_view_column_pack_start(col, renderer, FALSE);
		gtk_tree_view_column_set_cell_data_func(col, renderer, panel_cell_icon, panel, NULL);
		gtk_tree_view_column_set_expand(col, TRUE);
		g_ptr_array_add(panel->renderers, renderer);
	}
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "xalign", xalign, "ypad", 0, "xpad", 4, NULL);
	if (width_chars > 0)
		g_object_set(renderer, "width-chars", width_chars, NULL);
	if (which == VIEW_COL_NAME) {
		g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		panel->name_column = col;
		panel->name_renderer = renderer;
		g_signal_connect(renderer, "editing-started", G_CALLBACK(panel_on_editing_started), panel);
		g_signal_connect(renderer, "edited", G_CALLBACK(panel_on_edited), panel);
		g_signal_connect(renderer, "editing-canceled", G_CALLBACK(panel_on_editing_canceled), panel);
	}
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func(col, renderer, panel_cell_text, panel, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(panel->view), col);
	g_ptr_array_add(panel->renderers, renderer);
}

static GtkWidget *panel_small_button(const gchar *label, const gchar *tooltip)
{
	GtkWidget *button = gtk_button_new_with_label(label);

	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(button, FALSE);
	gtk_widget_set_tooltip_text(button, tooltip);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "unidesk-fm-small-button");
	return button;
}

static void panel_on_destroy(GtkWidget *widget, RemminaFTPPanel *panel)
{
	if (panel->entries)
		g_ptr_array_unref(panel->entries);
	g_hash_table_unref(panel->icons);
	g_object_unref(panel->icon_dir);
	g_object_unref(panel->icon_up);
	g_object_unref(panel->store);
	g_ptr_array_unref(panel->renderers);
	g_string_free(panel->quick, TRUE);
	g_free(panel->dir);
	g_free(panel->message);
	g_free(panel->rename_from);
	g_free(panel);
}

RemminaFTPPanel *remmina_ftp_panel_new(gboolean local, const RemminaFTPPanelCallbacks *callbacks)
{
	static const GtkTargetEntry uri_targets[] = { { (gchar *)FM_TARGET_URIS, 0, 0 } };
	/* The server panel's files: for our other panel, and as local paths for other programs */
	static const GtkTargetEntry remote_targets[] = {
		{ (gchar *)FM_TARGET_REMOTE, GTK_TARGET_SAME_APP, 1 }, { (gchar *)FM_TARGET_URIS, GTK_TARGET_OTHER_APP, 0 } };
	/* Our server panel's own target first: a drop from it is a copy job, not a copy of lent paths */
	static const GtkTargetEntry local_dest_targets[] = {
		{ (gchar *)FM_TARGET_REMOTE, GTK_TARGET_SAME_APP, 1 }, { (gchar *)FM_TARGET_URIS, 0, 0 } };
	RemminaFTPPanel *panel = g_new0(RemminaFTPPanel, 1);
	GtkWidget *line, *w, *scrolled;
	GtkTreeSortable *sortable;
	gint id;

	panel->local = local;
	if (callbacks)
		panel->cb = *callbacks;
	panel->quick = g_string_new(NULL);
	panel->renderers = g_ptr_array_new();
	panel->icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
	panel->icon_dir = g_themed_icon_new("folder");
	panel->icon_up = g_themed_icon_new("go-up");

	panel->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(panel->box), "unidesk-fm-panel");
	g_signal_connect(panel->box, "destroy", G_CALLBACK(panel_on_destroy), panel);

	/* Whose files these are; highlighted on the active panel */
	panel->title_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(panel->title_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(panel->title_label), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(panel->title_label), "unidesk-fm-title");
	gtk_box_pack_start(GTK_BOX(panel->box), panel->title_label, FALSE, FALSE, 0);

	/* The root and its free space, with "/" and ".." on the right, as the drive line of Total Commander */
	line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(line), "unidesk-fm-drive");
	w = gtk_label_new("/");
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "unidesk-fm-drive-name");
	gtk_box_pack_start(GTK_BOX(line), w, FALSE, FALSE, 0);
	panel->space_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(panel->space_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(panel->space_label), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(line), panel->space_label, TRUE, TRUE, 0);
	w = panel_small_button("/", _("Go to the root folder"));
	g_signal_connect(w, "clicked", G_CALLBACK(panel_on_root_clicked), panel);
	gtk_box_pack_start(GTK_BOX(line), w, FALSE, FALSE, 0);
	w = panel_small_button("..", _("Go to the parent folder"));
	g_signal_connect(w, "clicked", G_CALLBACK(panel_on_up_clicked), panel);
	gtk_box_pack_start(GTK_BOX(line), w, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(panel->box), line, FALSE, FALSE, 0);

	/* The path */
	panel->path_entry = gtk_entry_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(panel->path_entry), "unidesk-fm-path");
	g_signal_connect(panel->path_entry, "activate", G_CALLBACK(panel_on_path_activate), panel);
	g_signal_connect(panel->path_entry, "key-press-event", G_CALLBACK(panel_on_path_key), panel);
	g_signal_connect(panel->path_entry, "focus-in-event", G_CALLBACK(panel_on_focus_in), panel);
	gtk_box_pack_start(GTK_BOX(panel->box), panel->path_entry, FALSE, FALSE, 0);

	/* The list */
	panel->store = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN,
					  G_TYPE_UINT64, G_TYPE_INT64, G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_ICON, G_TYPE_STRING);
	sortable = GTK_TREE_SORTABLE(panel->store);
	for (id = SORT_NAME; id <= SORT_MODE; id++)
		gtk_tree_sortable_set_sort_func(sortable, id, panel_compare, GINT_TO_POINTER(id), NULL);
	gtk_tree_sortable_set_sort_column_id(sortable, SORT_NAME, GTK_SORT_ASCENDING);

	panel->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(panel->store));
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(panel->view), FALSE);
	gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(panel->view), TRUE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), GTK_SELECTION_BROWSE);
	/* TRANSLATORS: column headers of a file panel, as in Total Commander */
	panel_add_column(panel, VIEW_COL_NAME, C_("file panel", "Name"), SORT_NAME, 0.0, 10);
	panel_add_column(panel, VIEW_COL_EXT, C_("file panel", "Type"), SORT_EXT, 0.0, 4);
	/* The renderers take width-chars as their width; panel_fill fits it to the biggest file */
	panel_add_column(panel, VIEW_COL_SIZE, C_("file panel", "Size"), SORT_SIZE, 1.0, 8);
	panel->size_renderer = g_ptr_array_index(panel->renderers, panel->renderers->len - 1);
	panel_add_column(panel, VIEW_COL_DATE, C_("file panel", "Date"), SORT_DATE, 0.0, 16);
	panel_add_column(panel, VIEW_COL_MODE, C_("file panel", "Rights"), SORT_MODE, 0.0, 10);
	g_signal_connect(panel->view, "key-press-event", G_CALLBACK(panel_on_key_press), panel);
	g_signal_connect(panel->view, "button-press-event", G_CALLBACK(panel_on_button_press), panel);
	g_signal_connect(panel->view, "focus-in-event", G_CALLBACK(panel_on_focus_in), panel);
	g_signal_connect(panel->view, "cursor-changed", G_CALLBACK(panel_on_cursor_changed), panel);
	g_signal_connect(panel->view, "style-updated", G_CALLBACK(panel_on_style_updated), panel);
	panel_on_style_updated(panel->view, panel);

	gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(panel->view), GDK_BUTTON1_MASK,
					       local ? uri_targets : remote_targets, local ? 1 : 2, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect(panel->view, "drag-data-get", G_CALLBACK(panel_on_drag_data_get), panel);
	g_signal_connect_after(panel->view, "drag-begin", G_CALLBACK(panel_on_drag_begin), panel);
	g_signal_connect(panel->view, "drag-data-delete", G_CALLBACK(panel_on_drag_data_delete), panel);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled), panel->view);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_box_pack_start(GTK_BOX(panel->box), scrolled, TRUE, TRUE, 0);

	/* Drops land on the whole panel: into its current folder */
	gtk_drag_dest_set(panel->box, GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
			  local ? local_dest_targets : uri_targets, local ? 2 : 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect(panel->box, "drag-drop", G_CALLBACK(panel_on_drag_drop), panel);
	g_signal_connect(panel->box, "drag-data-received", G_CALLBACK(panel_on_drag_data_received), panel);

	/* The totals */
	panel->status_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(panel->status_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(panel->status_label), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(panel->status_label), "unidesk-fm-status");
	gtk_box_pack_start(GTK_BOX(panel->box), panel->status_label, FALSE, FALSE, 0);

	gtk_widget_show_all(panel->box);
	return panel;
}

GtkWidget *remmina_ftp_panel_get_widget(RemminaFTPPanel *panel)
{
	return panel->box;
}

gboolean remmina_ftp_panel_is_local(RemminaFTPPanel *panel)
{
	return panel->local;
}

void remmina_ftp_panel_set_fs(RemminaFTPPanel *panel, RemminaFTPFs *fs)
{
	panel->fs = fs;
	if (!fs) {
		gtk_list_store_clear(panel->store);
		g_free(panel->dir);
		panel->dir = NULL;
		panel_update_path(panel);
		panel_update_space(panel);
	}
}

RemminaFTPFs *remmina_ftp_panel_get_fs(RemminaFTPPanel *panel)
{
	return panel->fs;
}

const gchar *remmina_ftp_panel_get_dir(RemminaFTPPanel *panel)
{
	return panel->dir;
}

/* Owner and group come with the listing (an SFTP server names them only there) */
static void panel_fill_owner(RemminaFTPPanel *panel, RemminaFTPEntry *entry)
{
	guint i;

	for (i = 0; panel->entries && i < panel->entries->len; i++) {
		RemminaFTPEntry *e = g_ptr_array_index(panel->entries, i);
		if (g_strcmp0(e->name, entry->name) == 0) {
			entry->owner = g_strdup(e->owner);
			entry->group = g_strdup(e->group);
			return;
		}
	}
}

/* The entry of a row, NULL for ".." */
static RemminaFTPEntry *panel_entry_of(RemminaFTPPanel *panel, GtkTreeIter *iter)
{
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	RemminaFTPEntry *entry;
	gint kind;
	guint mode;

	gtk_tree_model_get(model, iter, COL_KIND, &kind, -1);
	if (kind == KIND_UP)
		return NULL;
	entry = remmina_ftp_entry_new();
	gtk_tree_model_get(model, iter, COL_NAME, &entry->name, COL_IS_LINK, &entry->is_link,
			   COL_SIZE, &entry->size, COL_MTIME, &entry->mtime, COL_MODE, &mode, -1);
	entry->type = kind == KIND_DIR ? REMMINA_FTP_ENTRY_DIR : REMMINA_FTP_ENTRY_FILE;
	entry->mode = mode;
	panel_fill_owner(panel, entry);
	return entry;
}

RemminaFTPEntry *remmina_ftp_panel_get_cursor_entry(RemminaFTPPanel *panel)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->view)), &model, &iter))
		return NULL;
	return panel_entry_of(panel, &iter);
}

GPtrArray *remmina_ftp_panel_get_targets(RemminaFTPPanel *panel)
{
	GPtrArray *targets = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_ftp_entry_free);
	GtkTreeModel *model = GTK_TREE_MODEL(panel->store);
	GtkTreeIter iter;
	gboolean valid;

	for (valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
		gboolean marked;
		RemminaFTPEntry *entry;
		gtk_tree_model_get(model, &iter, COL_MARKED, &marked, -1);
		if (marked && (entry = panel_entry_of(panel, &iter)))
			g_ptr_array_add(targets, entry);
	}
	if (targets->len == 0) {
		RemminaFTPEntry *entry = remmina_ftp_panel_get_cursor_entry(panel);
		if (entry)
			g_ptr_array_add(targets, entry);
	}
	if (targets->len == 0) {
		g_ptr_array_unref(targets);
		return NULL;
	}
	return targets;
}

void remmina_ftp_panel_select_name(RemminaFTPPanel *panel, const gchar *name)
{
	GtkTreeIter iter;

	if (panel_find_name(panel, name, &iter))
		panel_set_cursor_iter(panel, &iter);
}

void remmina_ftp_panel_set_show_hidden(RemminaFTPPanel *panel, gboolean show_hidden)
{
	GHashTable *marks;
	gchar *cursor;
	gint kind;

	if (panel->show_hidden == show_hidden)
		return;
	panel->show_hidden = show_hidden;
	if (!panel->entries)
		return;
	marks = panel_marked_names(panel);
	cursor = panel_cursor_name(panel, &kind);
	panel_fill(panel, marks, cursor);
	g_hash_table_unref(marks);
	g_free(cursor);
}

gboolean remmina_ftp_panel_get_show_hidden(RemminaFTPPanel *panel)
{
	return panel->show_hidden;
}

void remmina_ftp_panel_set_title(RemminaFTPPanel *panel, const gchar *title)
{
	gtk_label_set_text(GTK_LABEL(panel->title_label), title ? title : "");
}

void remmina_ftp_panel_set_active(RemminaFTPPanel *panel, gboolean active)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context(panel->box);

	panel->active = active;
	if (active)
		gtk_style_context_add_class(ctx, "active");
	else
		gtk_style_context_remove_class(ctx, "active");
	gtk_widget_queue_draw(panel->view);
}

void remmina_ftp_panel_grab_focus(RemminaFTPPanel *panel)
{
	gtk_widget_grab_focus(panel->view);
}

void remmina_ftp_panel_set_message(RemminaFTPPanel *panel, const gchar *message)
{
	g_free(panel->message);
	panel->message = g_strdup(message);
	panel_update_status(panel);
}
