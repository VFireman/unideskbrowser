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

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>

#include "remmina_ftp_dialogs.h"

/* ---- Helpers ---- */

static GtkWidget *dialog_new(GtkWindow *parent, const gchar *title, const gchar *ok_label)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							_("_Cancel"), GTK_RESPONSE_CANCEL, ok_label, GTK_RESPONSE_OK, NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "unidesk-fm-dialog");
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	gtk_box_set_spacing(GTK_BOX(content), 6);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	return dialog;
}

static GtkWidget *dialog_label(GtkWidget *box, const gchar *text)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 70);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	return label;
}

static GtkWidget *dialog_entry(GtkWidget *box, const gchar *text)
{
	GtkWidget *entry = gtk_entry_new();

	gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 50);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	return entry;
}

static gchar *dialog_entry_text(GtkWidget *entry)
{
	gchar *text = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));

	if (!text[0]) {
		g_free(text);
		return NULL;
	}
	return text;
}

/* "1 234 567" */
static gchar *dialog_group_digits(guint64 value)
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

static gchar *dialog_format_datetime(gint64 mtime)
{
	GDateTime *dt = g_date_time_new_from_unix_local(mtime);
	gchar *str;

	if (!dt)
		return g_strdup("");
	str = g_date_time_format(dt, "%d.%m.%Y %H:%M:%S");
	g_date_time_unref(dt);
	return str;
}

static const gchar *dialog_target_name(GPtrArray *targets)
{
	return ((RemminaFTPEntry *)g_ptr_array_index(targets, 0))->name;
}

/* ---- Simple questions ---- */

gchar *remmina_ftp_dialog_copy(GtkWindow *parent, gboolean move, GPtrArray *targets, const gchar *dst_dir)
{
	GtkWidget *dialog = dialog_new(parent, move ? _("Move Files") : _("Copy Files"), _("_OK"));
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *entry;
	gchar *text, *result = NULL;
	gchar *shown = g_str_has_suffix(dst_dir, "/") ? g_strconcat(dst_dir, "*.*", NULL) : g_strconcat(dst_dir, "/*.*", NULL);

	if (targets->len == 1) {
		gchar *name = g_filename_display_name(dialog_target_name(targets));
		text = g_strdup_printf(move ? _("Move “%s” to:") : _("Copy “%s” to:"), name);
		g_free(name);
	} else if (move) {
		text = g_strdup_printf(ngettext("Move %u file or folder to:", "Move %u files and folders to:", targets->len),
				       targets->len);
	} else {
		text = g_strdup_printf(ngettext("Copy %u file or folder to:", "Copy %u files and folders to:", targets->len),
				       targets->len);
	}
	dialog_label(content, text);
	g_free(text);
	entry = dialog_entry(content, shown);
	g_free(shown);
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		result = dialog_entry_text(entry);
		if (result && g_str_has_suffix(result, "*.*")) {
			result[strlen(result) - 3] = '\0';
		}
		if (result && strlen(result) > 1 && g_str_has_suffix(result, "/"))
			result[strlen(result) - 1] = '\0';
		if (result && !result[0]) {
			g_free(result);
			result = g_strdup("/");
		}
	}
	gtk_widget_destroy(dialog);
	return result;
}

gchar *remmina_ftp_dialog_mkdir(GtkWindow *parent)
{
	GtkWidget *dialog = dialog_new(parent, _("New Folder"), _("_OK"));
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *entry;
	gchar *result = NULL;

	dialog_label(content, _("New folder (folders):"));
	entry = dialog_entry(content, "");
	gtk_widget_show_all(dialog);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		result = dialog_entry_text(entry);
	gtk_widget_destroy(dialog);
	return result;
}

gboolean remmina_ftp_dialog_confirm_delete(GtkWindow *parent, GPtrArray *targets)
{
	GtkWidget *dialog;
	gint response;

	if (targets->len == 1) {
		gchar *name = g_filename_display_name(dialog_target_name(targets));
		dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
						GTK_BUTTONS_NONE, _("Delete “%s”?"), name);
		g_free(name);
	} else {
		dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
						GTK_BUTTONS_NONE,
						ngettext("Delete %u selected file or folder?",
							 "Delete %u selected files and folders?", targets->len),
						targets->len);
	}
	gtk_window_set_title(GTK_WINDOW(dialog), _("Delete Files"));
	gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Delete"), GTK_RESPONSE_OK, NULL);
	/* As in Total Commander, Enter confirms */
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(
					    gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK)),
				    "destructive-action");
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return response == GTK_RESPONSE_OK;
}

gchar *remmina_ftp_dialog_mask(GtkWindow *parent, gboolean mark)
{
	GtkWidget *dialog = dialog_new(parent, mark ? _("Select Files") : _("Unselect Files"), _("_OK"));
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *entry;
	gchar *result = NULL;

	dialog_label(content, mark ? _("Select the files that match:") : _("Unselect the files that match:"));
	entry = dialog_entry(content, "*.*");
	gtk_widget_show_all(dialog);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		result = dialog_entry_text(entry);
	gtk_widget_destroy(dialog);
	return result;
}

gboolean remmina_ftp_dialog_upload_changed(GtkWindow *parent, const gchar *name)
{
	gchar *display = g_filename_display_name(name);
	GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
						   GTK_BUTTONS_NONE, _("The file “%s” has changed. Upload it to the server?"), display);
	gint response;

	g_free(display);
	gtk_window_set_title(GTK_WINDOW(dialog), _("File Changed"));
	gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("_Do Not Upload"), GTK_RESPONSE_CANCEL, _("_Upload"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(
					    gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK)),
				    "suggested-action");
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return response == GTK_RESPONSE_OK;
}

void remmina_ftp_dialog_message(GtkWindow *parent, const gchar *message)
{
	GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
						   GTK_BUTTONS_OK, "%s", message);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

/* ---- File exists ---- */

enum {
	R_OVERWRITE = 1, R_OVERWRITE_ALL, R_SKIP, R_SKIP_ALL, R_OLDER, R_RENAME, R_RESUME, R_CANCEL, R_RETRY
};

static void dialog_on_button(GtkButton *button, GtkDialog *dialog)
{
	gtk_dialog_response(dialog, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response")));
}

static GtkWidget *dialog_grid_button(GtkWidget *grid, GtkWidget *dialog, const gchar *label, gint response, gint col, gint row)
{
	GtkWidget *button = gtk_button_new_with_mnemonic(label);

	g_object_set_data(G_OBJECT(button), "response", GINT_TO_POINTER(response));
	g_signal_connect(button, "clicked", G_CALLBACK(dialog_on_button), dialog);
	gtk_widget_set_hexpand(button, TRUE);
	gtk_grid_attach(GTK_GRID(grid), button, col, row, 1, 1);
	return button;
}

static gchar *dialog_file_line(const RemminaFTPEntry *entry)
{
	gchar *size = dialog_group_digits(entry->size);
	gchar *date = dialog_format_datetime(entry->mtime);
	/* TRANSLATORS: size in bytes and date of a file in the "file exists" dialog */
	gchar *line = g_strdup_printf(_("%s bytes    %s"), size, date);

	g_free(size);
	g_free(date);
	return line;
}

static gchar *dialog_ask_name(GtkWindow *parent, const gchar *name)
{
	GtkWidget *dialog = dialog_new(parent, _("Rename"), _("_OK"));
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *entry;
	gchar *result = NULL;
	const gchar *dot = strrchr(name, '.');

	dialog_label(content, _("New name:"));
	entry = dialog_entry(content, name);
	gtk_widget_show_all(dialog);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0,
				   dot && dot != name && g_utf8_validate(name, -1, NULL) ? g_utf8_pointer_to_offset(name, dot) : -1);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		result = dialog_entry_text(entry);
	gtk_widget_destroy(dialog);
	return result;
}

RemminaFTPConflict remmina_ftp_dialog_conflict(GtkWindow *parent, const gchar *src_path, const RemminaFTPEntry *src,
					       const gchar *dst_path, const RemminaFTPEntry *dst, gchar **new_name)
{
	GtkWidget *dialog = gtk_dialog_new();
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *grid, *button, *label;
	gchar *line, *shown;
	gint response;

	gtk_window_set_title(GTK_WINDOW(dialog), _("File Exists"));
	gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "unidesk-fm-dialog");
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	gtk_box_set_spacing(GTK_BOX(content), 6);

	label = dialog_label(content, _("Overwrite the file:"));
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "heading");
	shown = g_filename_display_name(dst_path);
	dialog_label(content, shown);
	g_free(shown);
	line = dialog_file_line(dst);
	dialog_label(content, line);
	g_free(line);

	label = dialog_label(content, _("with the file:"));
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "heading");
	shown = g_filename_display_name(src_path);
	dialog_label(content, shown);
	g_free(shown);
	line = dialog_file_line(src);
	dialog_label(content, line);
	g_free(line);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
	gtk_widget_set_margin_top(grid, 12);
	button = dialog_grid_button(grid, dialog, _("_Overwrite"), R_OVERWRITE, 0, 0);
	dialog_grid_button(grid, dialog, _("Overwrite _All"), R_OVERWRITE_ALL, 1, 0);
	dialog_grid_button(grid, dialog, _("_Skip"), R_SKIP, 2, 0);
	dialog_grid_button(grid, dialog, _("S_kip All"), R_SKIP_ALL, 3, 0);
	dialog_grid_button(grid, dialog, _("Overwrite All _Older"), R_OLDER, 0, 1);
	dialog_grid_button(grid, dialog, _("_Rename"), R_RENAME, 1, 1);
	gtk_widget_set_sensitive(dialog_grid_button(grid, dialog, _("Res_ume"), R_RESUME, 2, 1), dst->size < src->size);
	dialog_grid_button(grid, dialog, _("_Cancel"), R_CANCEL, 3, 1);
	gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);
	gtk_widget_grab_focus(button);
	for (;;) {
		response = gtk_dialog_run(GTK_DIALOG(dialog));
		if (response != R_RENAME)
			break;
		*new_name = dialog_ask_name(GTK_WINDOW(dialog), remmina_ftp_path_basename(dst_path));
		if (*new_name)
			break;
	}
	gtk_widget_destroy(dialog);

	switch (response) {
	case R_OVERWRITE:
		return REMMINA_FTP_CONFLICT_OVERWRITE;
	case R_OVERWRITE_ALL:
		return REMMINA_FTP_CONFLICT_OVERWRITE_ALL;
	case R_SKIP:
		return REMMINA_FTP_CONFLICT_SKIP;
	case R_SKIP_ALL:
		return REMMINA_FTP_CONFLICT_SKIP_ALL;
	case R_OLDER:
		return REMMINA_FTP_CONFLICT_OVERWRITE_OLDER_ALL;
	case R_RENAME:
		return REMMINA_FTP_CONFLICT_RENAME;
	case R_RESUME:
		return REMMINA_FTP_CONFLICT_RESUME;
	default:
		return REMMINA_FTP_CONFLICT_CANCEL;
	}
}

RemminaFTPErrorAnswer remmina_ftp_dialog_error(GtkWindow *parent, const gchar *message)
{
	GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
						   GTK_BUTTONS_NONE, "%s", message);
	gint response;

	gtk_window_set_title(GTK_WINDOW(dialog), _("Error"));
	gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("_Retry"), R_RETRY, _("_Skip"), R_SKIP, _("S_kip All"), R_SKIP_ALL,
			       _("_Cancel"), R_CANCEL, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), R_SKIP);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	switch (response) {
	case R_RETRY:
		return REMMINA_FTP_ERROR_RETRY;
	case R_SKIP:
		return REMMINA_FTP_ERROR_SKIP;
	case R_SKIP_ALL:
		return REMMINA_FTP_ERROR_SKIP_ALL;
	default:
		return REMMINA_FTP_ERROR_CANCEL;
	}
}

/* ---- Attributes (Alt+A) ---- */

#define ATTR_X_BITS (0100 | 010 | 01)

typedef struct {
	GtkWidget *	bits[12];	/* bit i is mode (1 << i) */
	GtkWidget *	exec;
	GtkWidget *	octal;
	GtkWidget *	text;
	guint32		known;		/* bits with a definite state (all the files agree, or the user set them) */
	guint32		value;
	gboolean	updating;
} AttrState;

static void attr_refresh(AttrState *st, gboolean from_octal)
{
	static const gchar letters[] = "xwrxwrxwr";
	gchar text[11];
	gint i;
	guint32 x_known = st->known & ATTR_X_BITS;

	st->updating = TRUE;
	for (i = 0; i < 12; i++) {
		guint32 bit = 1u << i;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->bits[i]), (st->value & bit) != 0);
		gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(st->bits[i]), !(st->known & bit));
	}
	gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(st->exec),
					   x_known != ATTR_X_BITS ||
					   ((st->value & ATTR_X_BITS) != 0 && (st->value & ATTR_X_BITS) != ATTR_X_BITS));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->exec), (st->value & ATTR_X_BITS) == ATTR_X_BITS);

	if (!from_octal) {
		if (st->known == 07777) {
			gchar oct[8];
			g_snprintf(oct, sizeof(oct), "%04o", st->value);
			gtk_entry_set_text(GTK_ENTRY(st->octal), oct);
		} else {
			gtk_entry_set_text(GTK_ENTRY(st->octal), "");
		}
	}

	/* "-rwsrwsrwt", "?" where the files differ */
	text[0] = '-';
	for (i = 8; i >= 0; i--) {
		guint32 bit = 1u << i;
		gchar c = (st->known & bit) ? ((st->value & bit) ? letters[i] : '-') : '?';
		text[9 - i] = c;
	}
	text[10] = '\0';
	if (st->known & 04000 && st->value & 04000)
		text[3] = text[3] == 'x' ? 's' : 'S';
	if (st->known & 02000 && st->value & 02000)
		text[6] = text[6] == 'x' ? 's' : 'S';
	if (st->known & 01000 && st->value & 01000)
		text[9] = text[9] == 'x' ? 't' : 'T';
	gtk_label_set_text(GTK_LABEL(st->text), text);
	st->updating = FALSE;
}

static void attr_on_bit_toggled(GtkToggleButton *button, AttrState *st)
{
	guint32 bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "bit"));

	if (st->updating)
		return;
	st->known |= bit;
	if (gtk_toggle_button_get_active(button))
		st->value |= bit;
	else
		st->value &= ~bit;
	attr_refresh(st, FALSE);
}

static void attr_on_exec_toggled(GtkToggleButton *button, AttrState *st)
{
	if (st->updating)
		return;
	st->known |= ATTR_X_BITS;
	if (gtk_toggle_button_get_active(button))
		st->value |= ATTR_X_BITS;
	else
		st->value &= ~ATTR_X_BITS;
	attr_refresh(st, FALSE);
}

static void attr_on_octal_changed(GtkEditable *editable, AttrState *st)
{
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(editable));
	gchar *end = NULL;
	guint64 value;

	if (st->updating || !text[0] || strlen(text) > 4)
		return;
	value = g_ascii_strtoull(text, &end, 8);
	if (!end || *end || value > 07777)
		return;
	st->known = 07777;
	st->value = (guint32)value;
	attr_refresh(st, TRUE);
}

static void attr_on_preset(GtkButton *button, AttrState *st)
{
	st->known = 07777;
	st->value = 07777;
	attr_refresh(st, FALSE);
}

static GtkWidget *attr_check(AttrState *st, gint bit_index, const gchar *label)
{
	GtkWidget *check = label ? gtk_check_button_new_with_mnemonic(label) : gtk_check_button_new();

	g_object_set_data(G_OBJECT(check), "bit", GUINT_TO_POINTER(1u << bit_index));
	g_signal_connect(check, "toggled", G_CALLBACK(attr_on_bit_toggled), st);
	st->bits[bit_index] = check;
	if (!label)
		gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
	return check;
}

static GtkWidget *attr_row_label(const gchar *text)
{
	GtkWidget *label = gtk_label_new_with_mnemonic(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0);
	return label;
}

static gboolean attr_parse_datetime(const gchar *text, gint64 *mtime)
{
	gint d, m, y, hh = 0, mm = 0, ss = 0;
	GDateTime *dt;

	if (sscanf(text, "%d.%d.%d %d:%d:%d", &d, &m, &y, &hh, &mm, &ss) < 3)
		return FALSE;
	dt = g_date_time_new_local(y, m, d, hh, mm, ss);
	if (!dt)
		return FALSE;
	*mtime = g_date_time_to_unix(dt);
	g_date_time_unref(dt);
	return TRUE;
}

gboolean remmina_ftp_dialog_attributes(GtkWindow *parent, GPtrArray *targets, RemminaFTPAttributes *result)
{
	GtkWidget *dialog = dialog_new(parent, _("Change Attributes"), _("_OK"));
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *grid, *w, *box, *date_check, *date_entry, *recursive = NULL;
	RemminaFTPEntry *first = g_ptr_array_index(targets, 0);
	AttrState st = { 0 };
	guint32 and_mode = 07777, or_mode = 0;
	gboolean any_dir = FALSE;
	gboolean ok = FALSE;
	gchar *text;
	guint i;
	gint row = 0;

	for (i = 0; i < targets->len; i++) {
		RemminaFTPEntry *entry = g_ptr_array_index(targets, i);
		and_mode &= entry->mode;
		or_mode |= entry->mode;
		any_dir |= entry->type == REMMINA_FTP_ENTRY_DIR;
	}
	st.known = ~(and_mode ^ or_mode) & 07777;
	st.value = and_mode & st.known;

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

	/* The file */
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("File name")), 0, row, 1, 1);
	if (targets->len == 1)
		text = g_filename_display_name(first->name);
	else
		text = g_strdup_printf(ngettext("%u file or folder", "%u files and folders", targets->len), targets->len);
	w = gtk_label_new(text);
	g_free(text);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_MIDDLE);
	gtk_grid_attach(GTK_GRID(grid), w, 1, row++, 3, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 4, 1);

	/* Read / Write / Execute for the owner, the group and the others */
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Read")), 1, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Write")), 2, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Execute")), 3, row++, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Owner")), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 8, NULL), 1, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 7, NULL), 2, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 6, NULL), 3, row++, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Group")), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 5, NULL), 1, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 4, NULL), 2, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 3, NULL), 3, row++, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Others")), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 2, NULL), 1, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 1, NULL), 2, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 0, NULL), 3, row++, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 4, 1);

	/* Special bits, executable, octal and text forms */
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Bits:")), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 11, "SUID"), 1, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 10, "SGID"), 2, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_check(&st, 9, _("Sticky bit")), 3, row++, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Launch:")), 0, row, 1, 1);
	st.exec = gtk_check_button_new_with_mnemonic(_("Allow e_xecuting as a program"));
	g_signal_connect(st.exec, "toggled", G_CALLBACK(attr_on_exec_toggled), &st);
	gtk_grid_attach(GTK_GRID(grid), st.exec, 1, row++, 3, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Octal:")), 0, row, 1, 1);
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	st.octal = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(st.octal), 6);
	gtk_entry_set_max_length(GTK_ENTRY(st.octal), 4);
	gtk_entry_set_placeholder_text(GTK_ENTRY(st.octal), "????");
	gtk_entry_set_activates_default(GTK_ENTRY(st.octal), TRUE);
	g_signal_connect(st.octal, "changed", G_CALLBACK(attr_on_octal_changed), &st);
	gtk_box_pack_start(GTK_BOX(box), st.octal, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("Text:")), FALSE, FALSE, 0);
	st.text = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(st.text), "monospace");
	gtk_box_pack_start(GTK_BOX(box), st.text, FALSE, FALSE, 0);
	/* TRANSLATORS: a button that sets every attribute (mode 7777) */
	w = gtk_button_new_with_label("7777");
	gtk_widget_set_tooltip_text(w, _("Set all the attributes"));
	g_signal_connect(w, "clicked", G_CALLBACK(attr_on_preset), &st);
	gtk_box_pack_end(GTK_BOX(box), w, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), box, 1, row++, 3, 1);

	/* Owner and group, shown only */
	w = gtk_label_new(_("Owner"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "heading");
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_widget_set_margin_top(w, 6);
	gtk_grid_attach(GTK_GRID(grid), w, 0, row++, 4, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Owner")), 0, row, 1, 1);
	w = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), first->owner ? first->owner : "—");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
	gtk_widget_set_sensitive(w, FALSE);
	gtk_grid_attach(GTK_GRID(grid), w, 1, row++, 3, 1);
	gtk_grid_attach(GTK_GRID(grid), attr_row_label(_("Group")), 0, row, 1, 1);
	w = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), first->group ? first->group : "—");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
	gtk_widget_set_sensitive(w, FALSE);
	gtk_grid_attach(GTK_GRID(grid), w, 1, row++, 3, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 4, 1);

	/* As in Total Commander: the date, and the folders' contents */
	date_check = gtk_check_button_new_with_mnemonic(_("Change the _date and time:"));
	gtk_grid_attach(GTK_GRID(grid), date_check, 0, row, 2, 1);
	text = dialog_format_datetime(first->mtime);
	date_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(date_entry), text);
	gtk_entry_set_activates_default(GTK_ENTRY(date_entry), TRUE);
	g_free(text);
	g_object_bind_property(date_check, "active", date_entry, "sensitive", G_BINDING_SYNC_CREATE);
	gtk_grid_attach(GTK_GRID(grid), date_entry, 2, row++, 2, 1);
	if (any_dir) {
		recursive = gtk_check_button_new_with_mnemonic(_("Including _subfolders"));
		gtk_grid_attach(GTK_GRID(grid), recursive, 0, row++, 4, 1);
	}

	attr_refresh(&st, FALSE);
	gtk_widget_show_all(dialog);

	while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		memset(result, 0, sizeof(*result));
		result->mode_set = st.value & st.known;
		result->mode_clear = ~st.value & st.known & 07777;
		result->recursive = recursive && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recursive));
		result->set_mtime = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(date_check));
		if (result->set_mtime && !attr_parse_datetime(gtk_entry_get_text(GTK_ENTRY(date_entry)), &result->mtime)) {
			remmina_ftp_dialog_message(GTK_WINDOW(dialog), _("The date must look like 16.10.2026 18:30:00."));
			continue;
		}
		ok = TRUE;
		break;
	}
	gtk_widget_destroy(dialog);
	return ok;
}

/* ---- Job progress ---- */

struct _RemminaFTPProgress {
	RemminaFTPJob *	job;
	GtkWidget *	window;
	GtkWidget *	from_label;
	GtkWidget *	to_label;
	GtkWidget *	file_bar;
	GtkWidget *	total_bar;
	GtkWidget *	info_label;
	GtkWidget *	pause_button;
	GCallback	on_background;
	gpointer	data;
};

const gchar *remmina_ftp_job_title(RemminaFTPJob *job)
{
	switch (job->op) {
	case REMMINA_FTP_JOB_MOVE:
		return _("Moving");
	case REMMINA_FTP_JOB_DELETE:
		return _("Deleting");
	case REMMINA_FTP_JOB_ATTRIBUTES:
		return _("Changing Attributes");
	case REMMINA_FTP_JOB_COPY:
	default:
		return _("Copying");
	}
}

void remmina_ftp_job_describe(RemminaFTPJob *job, gint *percent, gchar **speed, gchar **left)
{
	gboolean by_bytes = job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE;
	gdouble bps = remmina_ftp_job_speed(job);
	guint64 done, total;

	g_mutex_lock(&job->lock);
	done = by_bytes ? job->bytes_done : job->items_done;
	total = by_bytes ? job->bytes_total : job->items_total;
	g_mutex_unlock(&job->lock);

	*percent = total ? (gint)(MIN(done, total) * 100 / total) : 0;
	if (speed) {
		/* Kilobytes of 1024 bytes, as the panels count them (the units of g_format_size may stay untranslated) */
		if (!by_bytes || bps <= 0)
			*speed = g_strdup("");
		else if (bps < 1024.0 * 1024.0)
			/* TRANSLATORS: transfer speed in kilobytes per second */
			*speed = g_strdup_printf(_("%u KB/s"), (guint)(bps / 1024.0));
		else
			/* TRANSLATORS: transfer speed in megabytes per second */
			*speed = g_strdup_printf(_("%.1f MB/s"), bps / (1024.0 * 1024.0));
	}
	if (left) {
		if (by_bytes && bps > 0 && total > done) {
			guint64 secs = (guint64)((gdouble)(total - done) / bps);
			*left = g_strdup_printf("%02u:%02u:%02u", (guint)(secs / 3600), (guint)(secs / 60 % 60), (guint)(secs % 60));
		} else {
			*left = g_strdup("");
		}
	}
}

static void progress_on_pause(GtkButton *button, RemminaFTPProgress *progress)
{
	gboolean paused;

	g_mutex_lock(&progress->job->lock);
	paused = progress->job->paused;
	g_mutex_unlock(&progress->job->lock);
	remmina_ftp_job_set_paused(progress->job, !paused);
	remmina_ftp_progress_update(progress);
}

static void progress_on_cancel(GtkButton *button, RemminaFTPProgress *progress)
{
	remmina_ftp_job_set_paused(progress->job, FALSE);
	remmina_ftp_job_cancel(progress->job);
}

static void progress_to_background(RemminaFTPProgress *progress)
{
	gtk_widget_hide(progress->window);
	if (progress->on_background)
		((void (*)(RemminaFTPProgress *, gpointer))progress->on_background)(progress, progress->data);
}

static void progress_on_background(GtkButton *button, RemminaFTPProgress *progress)
{
	progress_to_background(progress);
}

/* Closing the window sends the job to the background, it does not stop it */
static gboolean progress_on_delete(GtkWidget *widget, GdkEvent *event, RemminaFTPProgress *progress)
{
	progress_to_background(progress);
	return TRUE;
}

static GtkWidget *progress_label(GtkWidget *box)
{
	GtkWidget *label = gtk_label_new("");

	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_width_chars(GTK_LABEL(label), 60);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	return label;
}

RemminaFTPProgress *remmina_ftp_progress_new(GtkWindow *parent, RemminaFTPJob *job, GCallback on_background, gpointer data)
{
	RemminaFTPProgress *progress = g_new0(RemminaFTPProgress, 1);
	GtkWidget *box, *buttons, *w;
	gboolean bytes = job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE;

	progress->job = job;
	progress->on_background = on_background;
	progress->data = data;

	progress->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(progress->window), remmina_ftp_job_title(job));
	gtk_window_set_transient_for(GTK_WINDOW(progress->window), parent);
	gtk_window_set_modal(GTK_WINDOW(progress->window), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(progress->window), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(progress->window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_position(GTK_WINDOW(progress->window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_style_context_add_class(gtk_widget_get_style_context(progress->window), "unidesk-fm-dialog");
	g_signal_connect(progress->window, "delete-event", G_CALLBACK(progress_on_delete), progress);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_container_add(GTK_CONTAINER(progress->window), box);

	progress->from_label = progress_label(box);
	progress->to_label = progress_label(box);
	progress->file_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress->file_bar), TRUE);
	gtk_box_pack_start(GTK_BOX(box), progress->file_bar, FALSE, FALSE, 0);
	progress->total_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress->total_bar), TRUE);
	gtk_box_pack_start(GTK_BOX(box), progress->total_bar, FALSE, FALSE, 0);
	progress->info_label = progress_label(box);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_set_homogeneous(GTK_BOX(buttons), TRUE);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_widget_set_margin_top(buttons, 6);
	progress->pause_button = gtk_button_new_with_mnemonic(_("_Pause"));
	g_signal_connect(progress->pause_button, "clicked", G_CALLBACK(progress_on_pause), progress);
	gtk_box_pack_start(GTK_BOX(buttons), progress->pause_button, TRUE, TRUE, 0);
	w = gtk_button_new_with_mnemonic(_("In _Background"));
	g_signal_connect(w, "clicked", G_CALLBACK(progress_on_background), progress);
	gtk_box_pack_start(GTK_BOX(buttons), w, TRUE, TRUE, 0);
	w = gtk_button_new_with_mnemonic(_("_Cancel"));
	g_signal_connect(w, "clicked", G_CALLBACK(progress_on_cancel), progress);
	gtk_box_pack_start(GTK_BOX(buttons), w, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

	gtk_widget_show_all(progress->window);
	if (!bytes) {
		gtk_widget_hide(progress->to_label);
		gtk_widget_hide(progress->file_bar);
	}
	remmina_ftp_progress_update(progress);
	return progress;
}

void remmina_ftp_progress_update(RemminaFTPProgress *progress)
{
	RemminaFTPJob *job = progress->job;
	gboolean bytes = job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE;
	gchar *src, *dst, *text, *speed, *left;
	guint64 file_done, file_total;
	guint items_done, items_total;
	RemminaFTPJobState state;
	gboolean paused;
	gint percent;

	g_mutex_lock(&job->lock);
	src = g_strdup(job->current_src);
	dst = g_strdup(job->current_dst);
	file_done = job->file_done;
	file_total = job->file_total;
	items_done = job->items_done;
	items_total = job->items_total;
	state = job->state;
	paused = job->paused;
	g_mutex_unlock(&job->lock);

	remmina_ftp_job_describe(job, &percent, &speed, &left);

	text = g_strdup_printf("%s (%d %%)", remmina_ftp_job_title(job), percent);
	gtk_window_set_title(GTK_WINDOW(progress->window), text);
	g_free(text);

	if (state == REMMINA_FTP_JOB_QUEUED || state == REMMINA_FTP_JOB_SCANNING) {
		gtk_label_set_text(GTK_LABEL(progress->from_label),
				   state == REMMINA_FTP_JOB_QUEUED ? _("Waiting for the previous job…") : _("Counting the files…"));
		gtk_label_set_text(GTK_LABEL(progress->to_label), "");
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress->total_bar));
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress->total_bar), "");
	} else {
		gchar *s = src ? g_filename_display_name(src) : g_strdup("");
		gchar *d = dst ? g_filename_display_name(dst) : g_strdup("");
		text = g_strdup_printf("%s %s", remmina_ftp_job_title(job), s);
		gtk_label_set_text(GTK_LABEL(progress->from_label), text);
		g_free(text);
		/* TRANSLATORS: second line of the progress window: "to /path/file" */
		text = g_strdup_printf(_("to %s"), d);
		gtk_label_set_text(GTK_LABEL(progress->to_label), text);
		g_free(text);
		g_free(s);
		g_free(d);

		if (bytes) {
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress->file_bar),
						      file_total ? (gdouble)MIN(file_done, file_total) / (gdouble)file_total : 0);
			text = g_strdup_printf("%d %%", file_total ? (gint)(MIN(file_done, file_total) * 100 / file_total) : 0);
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress->file_bar), text);
			g_free(text);
		}
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress->total_bar), percent / 100.0);
		text = g_strdup_printf("%d %%", percent);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress->total_bar), text);
		g_free(text);
	}

	/* TRANSLATORS: last line of the progress window: speed, time left, items done */
	text = bytes && speed[0] ?
	       g_strdup_printf(_("Speed: %s    Left: %s    Files: %u of %u"), speed, left, items_done, items_total) :
	       g_strdup_printf(_("Files: %u of %u"), items_done, items_total);
	gtk_label_set_text(GTK_LABEL(progress->info_label), text);
	g_free(text);
	gtk_button_set_label(GTK_BUTTON(progress->pause_button), paused ? _("_Resume") : _("_Pause"));

	g_free(src);
	g_free(dst);
	g_free(speed);
	g_free(left);
}

void remmina_ftp_progress_present(RemminaFTPProgress *progress)
{
	gtk_window_present(GTK_WINDOW(progress->window));
}

GtkWindow *remmina_ftp_progress_get_window(RemminaFTPProgress *progress)
{
	return GTK_WINDOW(progress->window);
}

gboolean remmina_ftp_progress_is_visible(RemminaFTPProgress *progress)
{
	return gtk_widget_get_visible(progress->window);
}

void remmina_ftp_progress_free(RemminaFTPProgress *progress)
{
	if (!progress)
		return;
	gtk_widget_destroy(progress->window);
	g_free(progress);
}
