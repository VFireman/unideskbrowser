/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2010 Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
 * Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
 * Copyright (C) 2023-2024 Hiroyuki Tanaka, Sunil Bhat
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU General Public License in all respects
 *  for all of the code used other than OpenSSL. *  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so. *  If you
 *  do not wish to do so, delete this exception statement from your
 *  version. *  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "remmina_public.h"
#include "remmina_pref.h"
#include "remmina_log.h"
#include "rcw.h"
#include "remmina/remmina_trace_calls.h"

gboolean logstart;

/***** Define the log window GUI *****/
#define REMMINA_TYPE_LOG_WINDOW               (remmina_log_window_get_type())
#define REMMINA_LOG_WINDOW(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj), REMMINA_TYPE_LOG_WINDOW, RemminaLogWindow))
#define REMMINA_LOG_WINDOW_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), REMMINA_TYPE_LOG_WINDOW, RemminaLogWindowClass))
#define REMMINA_IS_LOG_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), REMMINA_TYPE_LOG_WINDOW))
#define REMMINA_IS_LOG_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass), REMMINA_TYPE_LOG_WINDOW))
#define REMMINA_LOG_WINDOW_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS((obj), REMMINA_TYPE_LOG_WINDOW, RemminaLogWindowClass))

typedef struct _RemminaLogWindow {
	GtkWindow window;

	GtkWidget *log_view;
	GtkTextBuffer *log_buffer;
} RemminaLogWindow;

typedef struct _RemminaLogWindowClass {
	GtkWindowClass parent_class;
} RemminaLogWindowClass;

GType remmina_log_window_get_type(void)
G_GNUC_CONST;

G_DEFINE_TYPE(RemminaLogWindow, remmina_log_window, GTK_TYPE_WINDOW)

static void remmina_log_window_class_init(RemminaLogWindowClass *klass)
{
	TRACE_CALL(__func__);
}

/* We will always only have one log window per instance */
static GtkWidget *log_window = NULL;

static GtkWidget*
remmina_log_window_new(void)
{
	TRACE_CALL(__func__);
	return GTK_WIDGET(g_object_new(REMMINA_TYPE_LOG_WINDOW, NULL));
}

static void remmina_log_end(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	log_window = NULL;
}

static void remmina_log_start_stop (GtkSwitch *logswitch, gpointer user_data)
{
	TRACE_CALL(__func__);
	logstart = !logstart;
}

void remmina_log_start(void)
{
	TRACE_CALL(__func__);
	if (log_window) {
		/* Switch to the existing debugging tab */
		rcw_main_add_window_as_tab(GTK_WINDOW(log_window), _("Debugging"), "utilities-terminal-symbolic");
	}else  {
		GtkWidget *content, *vbox, *hbox, *start, *label;

		log_window = remmina_log_window_new();

		/* Unidesk: the window is hosted as a tab, so the On/Off switch sits
		 * above the log instead of in a header bar */
		content = gtk_bin_get_child(GTK_BIN(log_window));
		g_object_ref(content);
		gtk_container_remove(GTK_CONTAINER(log_window), content);

		vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_container_set_border_width(GTK_CONTAINER(hbox), 6);
		label = gtk_label_new(_("Logging"));
		start = gtk_switch_new ();
		logstart = TRUE;
		gtk_switch_set_active (GTK_SWITCH(start), logstart);
		gtk_widget_set_valign (start, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), start, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), content, TRUE, TRUE, 0);
		g_object_unref(content);
		gtk_container_add(GTK_CONTAINER(log_window), vbox);
		gtk_widget_show_all(vbox);

		g_signal_connect(start, "notify::active", G_CALLBACK(remmina_log_start_stop), NULL);
		g_signal_connect(G_OBJECT(log_window), "destroy", G_CALLBACK(remmina_log_end), NULL);
		rcw_main_add_window_as_tab(GTK_WINDOW(log_window), _("Debugging"), "utilities-terminal-symbolic");
	}

	remmina_log_print(_("This window can help you find connection problems.\n"
		"You can stop and start the logging at any moment using the On/Off switch.\n"
		));
}

gboolean remmina_log_running(void)
{
	TRACE_CALL(__func__);
	return (log_window != NULL);
}

static gboolean remmina_log_scroll_to_end(gpointer data)
{
	TRACE_CALL(__func__);
	GtkTextIter iter;

	if (log_window) {
		gtk_text_buffer_get_end_iter(REMMINA_LOG_WINDOW(log_window)->log_buffer, &iter);
		gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(REMMINA_LOG_WINDOW(log_window)->log_view), &iter, 0.0, FALSE, 0.0,
			0.0);
	}
	return FALSE;
}

static gboolean remmina_log_print_real(gpointer data)
{
	TRACE_CALL(__func__);
	GtkTextIter iter;

	if (log_window && logstart) {
		gtk_text_buffer_get_end_iter(REMMINA_LOG_WINDOW(log_window)->log_buffer, &iter);
		gtk_text_buffer_insert(REMMINA_LOG_WINDOW(log_window)->log_buffer, &iter, (const gchar*)data, -1);
		IDLE_ADD(remmina_log_scroll_to_end, NULL);
	}
	g_free(data);
	return FALSE;
}

// Only prints into Remmina's own debug window. (Not stdout!)
// See _remmina_{debug, info, error, critical, warning}
void remmina_log_print(const gchar *text)
{
	TRACE_CALL(__func__);
	if (!log_window)
		return;

	IDLE_ADD(remmina_log_print_real, g_strdup(text));
}

void remmina_log_file_append(gchar *text) {
	gchar *log_filename = g_build_filename(g_get_tmp_dir(), LOG_FILE_NAME, NULL);
	FILE *log_file = fopen(log_filename, "a");
	g_free(log_filename);
	
	if (log_file != NULL)
	{
		gchar* text_log = g_strconcat(text, "\n", NULL);
		fwrite(text_log, sizeof(char), strlen(text_log), log_file);
		fclose(log_file);
		g_free(text_log);
	}
}

void _remmina_info(const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	g_autofree gchar *text;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	// always appends newline
	g_info ("%s", text);

	g_autofree gchar *buf_tmp = g_strconcat(text, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(INFO) - ", buf_tmp, NULL);

	if (!log_window) {
		free(bufn);
		return;
	}
	IDLE_ADD(remmina_log_print_real, bufn);
}

void _remmina_message(const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	va_start(args, fmt);
	g_autofree gchar * text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	// always appends newline
	g_message ("%s", text);

	if (!log_window) {
		return;
	}

	g_autofree gchar *buf_tmp = g_strconcat(text, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(MESSAGE) - ", buf_tmp, NULL);

	IDLE_ADD(remmina_log_print_real, bufn);
}

/**
 * Print a string in the Remmina Debug Windows and in the terminal.
 * The string will be visible in the terminal if G_MESSAGES_DEBUG=remmina
 * Variadic function of REMMINA_DEBUG
 */
void _remmina_debug(const gchar *fun, const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	gchar *text;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	g_autofree gchar *buf = g_strconcat("(", fun, ") - ", text, NULL);
	g_free(text);

	// always appends newline
	g_debug ("%s", buf);

	if (!log_window) {
		return;
	}

	g_autofree gchar *buf_tmp = g_strconcat(buf, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(DEBUG) - ", buf_tmp, NULL);

	IDLE_ADD(remmina_log_print_real, bufn);
}

void _remmina_warning(const gchar *fun, const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	gchar *text;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	g_autofree gchar *buf = g_strconcat("(", fun, ") - ", text, NULL);
	g_free(text);

	// always appends newline
	g_warning ("%s", buf);

	if (!log_window) {
		return;
	}

	g_autofree gchar *buf_tmp = g_strconcat(buf, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(WARN) - ", buf_tmp, NULL);

	IDLE_ADD(remmina_log_print_real, bufn);
}

void _remmina_audit(const gchar *fun, const gchar *fmt, ...)
{
	TRACE_CALL(__func__);
	va_list args;
	va_start(args, fmt);
	gchar *text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

#if GLIB_CHECK_VERSION(2,62,0)
	GDateTime* tv = g_date_time_new_now_local();
	gchar *isodate = g_date_time_format_iso8601(tv);
	g_date_time_unref(tv);
#else
	GTimeVal tv;
	g_get_current_time(&tv);
	gchar *isodate = g_time_val_to_iso8601(&tv);
#endif

	gchar *buf = g_strdup("");

	if (isodate) {
		g_free(buf);
		buf = g_strconcat(
				"[", isodate, "] - ",
				g_get_host_name (),
				" - ",
				g_get_user_name (),
				" - ",
				text,
				NULL);

	}

	g_free(text);
	if (remmina_pref_get_boolean("audit"))
		_remmina_message(buf);
	else
		_remmina_debug(fun, "%s", buf);
	g_free(buf);
	g_free(isodate);
}

// !!! Calling this function will crash Remmina !!!
// !!! purposefully and send a trap signal !!!
void _remmina_error(const gchar *fun, const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	gchar *text;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	g_autofree gchar *buf = g_strconcat("(", fun, ") - ", text, NULL);
	g_free(text);

	// always appends newline
	g_error ("%s", buf);

	if (!log_window) {
		return;
	}

	g_autofree gchar *buf_tmp = g_strconcat(buf, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(ERROR) - ", buf_tmp, NULL);

	IDLE_ADD(remmina_log_print_real, bufn);
}

void _remmina_critical(const gchar *fun, const gchar *fmt, ...)
{
	TRACE_CALL(__func__);

	va_list args;
	gchar *text;
	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	// Append text to remmina_log_file.log
	remmina_log_file_append(text);

	g_autofree gchar *buf = g_strconcat("(", fun, ") - ", text, NULL);
	g_free(text);

	// always appends newline
	g_critical ("%s", buf);

	if (!log_window) {
		return;
	}

	g_autofree gchar *buf_tmp = g_strconcat(buf, "\n", NULL);
	/* freed in remmina_log_print_real */
	gchar *bufn = g_strconcat("(CRIT) - ", buf_tmp, NULL);

	IDLE_ADD(remmina_log_print_real, bufn);
}

// Only prints into Remmina's own debug window. (Not stdout!)
// See _remmina_{message, info, debug warning, error, critical}
void remmina_log_printf(const gchar *fmt, ...)
{
	TRACE_CALL(__func__);
	va_list args;
	gchar *text;

	if (!log_window) return;

	va_start(args, fmt);
	text = g_strdup_vprintf(fmt, args);
	va_end(args);

	IDLE_ADD(remmina_log_print_real, text);
}

static gboolean remmina_log_on_keypress(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	TRACE_CALL(__func__);

	if (!log_window)
		return FALSE;

	GdkEventKey *e = (GdkEventKey *)event;

	if ((e->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		return TRUE;

	return FALSE;
}

static void remmina_log_window_init(RemminaLogWindow *logwin)
{
	TRACE_CALL(__func__);
	GtkWidget *scrolledwindow;
	GtkWidget *widget;

	gtk_container_set_border_width(GTK_CONTAINER(logwin), 4);

	scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(scrolledwindow);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(logwin), scrolledwindow);

	widget = gtk_text_view_new();
	gtk_widget_show(widget);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(widget), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(widget), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(widget), TRUE);
	gtk_container_add(GTK_CONTAINER(scrolledwindow), widget);
	logwin->log_view = widget;
	logwin->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));

	g_signal_connect(G_OBJECT(logwin->log_view), "key-press-event", G_CALLBACK(remmina_log_on_keypress), (gpointer)logwin);
}

