/*
 * Unidesk - Telnet protocol plugin (built-in)
 *
 * A TELNET (RFC 854) client on top of GIO sockets, libtelnet (public domain,
 * vendored in src/libtelnet.c) and a VteTerminal widget. All socket I/O is
 * asynchronous on the GTK main loop, so VTE is only ever touched from the
 * main thread.
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

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#include "remmina_public.h"
#include "remmina_plugin_manager.h"
#include "remmina_protocol_widget.h"
#include "remmina_pref.h"
#include "remmina_log.h"
#include "remmina_ssh_plugin.h"
#include "remmina_telnet_plugin.h"
#include "remmina/remmina_trace_calls.h"

#if defined(HAVE_LIBVTE) && defined(HAVE_LIBSSH)

#include <vte/vte.h>
#include "libtelnet.h"

#define GET_PLUGIN_DATA(gp) (RemminaPluginTelnetData *)g_object_get_data(G_OBJECT(gp), "plugin-data")

#define TELNET_DEFAULT_PORT	23
#define TELNET_TERMINAL_TYPE	"xterm-256color"
#define TELNET_READ_BUFFER	16384
#define TELNET_PROMPT_WINDOW	256
#define SCALE_FACTOR		0.1

typedef struct _RemminaPluginTelnetData {
	GtkWidget *		vte;

	GSocketClient *		client;
	GSocketConnection *	connection;
	GInputStream *		istream;        /* owned by connection */
	GOutputStream *		ostream;        /* owned by connection */
	GCancellable *		cancellable;

	telnet_t *		telnet;
	guchar			rbuf[TELNET_READ_BUFFER];
	GByteArray *		wqueue;         /* bytes waiting to be written */
	GBytes *		wchunk;         /* bytes currently being written */
	gboolean		write_pending;

	gboolean		remote_echo;    /* the server has agreed to echo (WILL ECHO) */
	gboolean		naws_enabled;   /* the server asked for window size updates */
	gboolean		no_local_echo;  /* profile setting */
	gint			last_cols, last_rows;

	gboolean		connected;
	gboolean		closed;

	/* automatic login driven by the prompts printed by the server */
	gchar *			username;
	gchar *			password;
	gboolean		user_sent;
	gboolean		pass_sent;
	GString *		recent;

	gchar *			host;
	gint			port;
} RemminaPluginTelnetData;

static RemminaPluginService *remmina_plugin_service = NULL;

static const telnet_telopt_t remmina_telnet_telopts[] = {
	{ TELNET_TELOPT_ECHO,  TELNET_WONT, TELNET_DO   },
	{ TELNET_TELOPT_SGA,   TELNET_WILL, TELNET_DO   },
	{ TELNET_TELOPT_TTYPE, TELNET_WILL, TELNET_DONT },
	{ TELNET_TELOPT_NAWS,  TELNET_WILL, TELNET_DONT },
	{ -1, 0, 0 }
};

static void remmina_telnet_flush_write(RemminaProtocolWidget *gp);
static void remmina_telnet_start_read(RemminaProtocolWidget *gp);
static void remmina_telnet_disconnected(RemminaProtocolWidget *gp, const gchar *error);

/* ------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* ------------------------------------------------------------------------- */

static void remmina_telnet_feed_message(RemminaPluginTelnetData *gpdata, const gchar *msg)
{
	gchar *line;

	if (!gpdata || !gpdata->vte)
		return;
	line = g_strdup_printf("\r\n\033[1;33m[UniDeskBrowser] %s\033[0m\r\n", msg);
	vte_terminal_feed(VTE_TERMINAL(gpdata->vte), line, -1);
	g_free(line);
}

static void remmina_telnet_data_free(gpointer data)
{
	RemminaPluginTelnetData *gpdata = (RemminaPluginTelnetData *)data;

	if (!gpdata)
		return;
	if (gpdata->telnet) {
		telnet_free(gpdata->telnet);
		gpdata->telnet = NULL;
	}
	g_clear_object(&gpdata->cancellable);
	g_clear_object(&gpdata->connection);
	g_clear_object(&gpdata->client);
	g_clear_pointer(&gpdata->wchunk, g_bytes_unref);
	if (gpdata->wqueue)
		g_byte_array_free(gpdata->wqueue, TRUE);
	if (gpdata->recent)
		g_string_free(gpdata->recent, TRUE);
	g_free(gpdata->username);
	g_free(gpdata->password);
	g_free(gpdata->host);
	g_free(gpdata);
}

static gboolean remmina_telnet_str_ends_with_prompt(const gchar *text, const gchar *prompt)
{
	gsize tl = strlen(text), pl = strlen(prompt);

	if (tl < pl)
		return FALSE;
	return strcmp(text + tl - pl, prompt) == 0;
}

/* ------------------------------------------------------------------------- */
/* Socket writing                                                              */
/* ------------------------------------------------------------------------- */

static void remmina_telnet_on_write_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GError *error = NULL;
	gsize written = 0;

	g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), res, &written, &error);

	if (gpdata) {
		gpdata->write_pending = FALSE;
		g_clear_pointer(&gpdata->wchunk, g_bytes_unref);
		if (error) {
			if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) && !gpdata->closed)
				remmina_telnet_disconnected(gp, error->message);
		} else if (!gpdata->closed) {
			remmina_telnet_flush_write(gp);
		}
	}
	g_clear_error(&error);
	g_object_unref(gp);
}

static void remmina_telnet_flush_write(RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || gpdata->closed || gpdata->write_pending || !gpdata->ostream)
		return;
	if (gpdata->wqueue->len == 0)
		return;

	/* Move the queued bytes into an immutable chunk: the buffer passed to
	 * g_output_stream_write_all_async must stay valid until completion, while
	 * new bytes may be queued (and the array reallocated) meanwhile. */
	gpdata->wchunk = g_bytes_new(gpdata->wqueue->data, gpdata->wqueue->len);
	g_byte_array_set_size(gpdata->wqueue, 0);
	gpdata->write_pending = TRUE;
	g_output_stream_write_all_async(gpdata->ostream,
					g_bytes_get_data(gpdata->wchunk, NULL), g_bytes_get_size(gpdata->wchunk),
					G_PRIORITY_DEFAULT, gpdata->cancellable,
					remmina_telnet_on_write_done, g_object_ref(gp));
}

/* ------------------------------------------------------------------------- */
/* TELNET protocol events                                                      */
/* ------------------------------------------------------------------------- */

static void remmina_telnet_send_naws(RemminaProtocolWidget *gp, gboolean force)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	gint cols, rows;
	guchar buf[4];

	if (!gpdata || !gpdata->telnet || !gpdata->naws_enabled || !gpdata->vte)
		return;
	cols = (gint)vte_terminal_get_column_count(VTE_TERMINAL(gpdata->vte));
	rows = (gint)vte_terminal_get_row_count(VTE_TERMINAL(gpdata->vte));
	if (cols <= 0 || rows <= 0)
		return;
	if (!force && cols == gpdata->last_cols && rows == gpdata->last_rows)
		return;
	gpdata->last_cols = cols;
	gpdata->last_rows = rows;
	buf[0] = (cols >> 8) & 0xff;
	buf[1] = cols & 0xff;
	buf[2] = (rows >> 8) & 0xff;
	buf[3] = rows & 0xff;
	telnet_subnegotiation(gpdata->telnet, TELNET_TELOPT_NAWS, (const char *)buf, sizeof(buf));
}

static void remmina_telnet_send_line(RemminaPluginTelnetData *gpdata, const gchar *text)
{
	if (!gpdata->telnet)
		return;
	telnet_send_text(gpdata->telnet, text, strlen(text));
	telnet_send_text(gpdata->telnet, "\r", 1);
}

/* Look at the tail of what the server printed and answer login/password prompts */
static void remmina_telnet_autologin_check(RemminaProtocolWidget *gp, const char *buffer, size_t size)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	gchar *low;
	gsize len;

	if (!gpdata->username && !gpdata->password)
		return;
	if (gpdata->user_sent && gpdata->pass_sent)
		return;

	g_string_append_len(gpdata->recent, buffer, size);
	if (gpdata->recent->len > TELNET_PROMPT_WINDOW)
		g_string_erase(gpdata->recent, 0, gpdata->recent->len - TELNET_PROMPT_WINDOW);

	low = g_ascii_strdown(gpdata->recent->str, gpdata->recent->len);
	/* ignore trailing blanks after the prompt */
	len = strlen(low);
	while (len > 0 && (low[len - 1] == ' ' || low[len - 1] == '\t'))
		low[--len] = '\0';

	if (!gpdata->pass_sent && gpdata->password && gpdata->password[0] &&
	    remmina_telnet_str_ends_with_prompt(low, "password:")) {
		remmina_telnet_send_line(gpdata, gpdata->password);
		gpdata->pass_sent = TRUE;
		if (!gpdata->username || !gpdata->username[0])
			gpdata->user_sent = TRUE;
		g_string_set_size(gpdata->recent, 0);
	} else if (!gpdata->user_sent && gpdata->username && gpdata->username[0] &&
		   (remmina_telnet_str_ends_with_prompt(low, "login:") ||
		    remmina_telnet_str_ends_with_prompt(low, "username:") ||
		    remmina_telnet_str_ends_with_prompt(low, "user name:") ||
		    remmina_telnet_str_ends_with_prompt(low, "user:"))) {
		remmina_telnet_send_line(gpdata, gpdata->username);
		gpdata->user_sent = TRUE;
		if (!gpdata->password || !gpdata->password[0])
			gpdata->pass_sent = TRUE;
		g_string_set_size(gpdata->recent, 0);
	}
	g_free(low);
}

static void remmina_telnet_event_handler(telnet_t *telnet, telnet_event_t *ev, void *user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || gpdata->closed)
		return;

	switch (ev->type) {
	case TELNET_EV_DATA:
		if (ev->data.size > 0 && gpdata->vte) {
			vte_terminal_feed(VTE_TERMINAL(gpdata->vte), ev->data.buffer, (glong)ev->data.size);
			remmina_telnet_autologin_check(gp, ev->data.buffer, ev->data.size);
		}
		break;
	case TELNET_EV_SEND:
		if (ev->data.size > 0) {
			g_byte_array_append(gpdata->wqueue, (const guint8 *)ev->data.buffer, (guint)ev->data.size);
			remmina_telnet_flush_write(gp);
		}
		break;
	case TELNET_EV_WILL:
		if (ev->neg.telopt == TELNET_TELOPT_ECHO)
			gpdata->remote_echo = TRUE;
		break;
	case TELNET_EV_WONT:
		if (ev->neg.telopt == TELNET_TELOPT_ECHO)
			gpdata->remote_echo = FALSE;
		break;
	case TELNET_EV_DO:
		if (ev->neg.telopt == TELNET_TELOPT_NAWS) {
			gpdata->naws_enabled = TRUE;
			remmina_telnet_send_naws(gp, TRUE);
		}
		break;
	case TELNET_EV_DONT:
		if (ev->neg.telopt == TELNET_TELOPT_NAWS)
			gpdata->naws_enabled = FALSE;
		break;
	case TELNET_EV_TTYPE:
		if (ev->ttype.cmd == TELNET_TTYPE_SEND)
			telnet_ttype_is(telnet, TELNET_TERMINAL_TYPE);
		break;
	case TELNET_EV_ERROR:
		REMMINA_WARNING("telnet protocol error: %s", ev->error.msg ? ev->error.msg : "unknown");
		remmina_telnet_disconnected(gp, ev->error.msg);
		break;
	case TELNET_EV_WARNING:
		REMMINA_DEBUG("telnet protocol warning: %s", ev->error.msg ? ev->error.msg : "unknown");
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------------- */
/* Socket reading / connection life cycle                                      */
/* ------------------------------------------------------------------------- */

static void remmina_telnet_on_read_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GError *error = NULL;
	gssize n;

	n = g_input_stream_read_finish(G_INPUT_STREAM(source), res, &error);

	if (gpdata && !gpdata->closed) {
		if (n > 0) {
			telnet_recv(gpdata->telnet, (const char *)gpdata->rbuf, (size_t)n);
			if (!gpdata->closed)
				remmina_telnet_start_read(gp);
		} else if (n == 0) {
			remmina_telnet_disconnected(gp, NULL);
		} else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			remmina_telnet_disconnected(gp, error ? error->message : NULL);
		}
	}
	g_clear_error(&error);
	g_object_unref(gp);
}

static void remmina_telnet_start_read(RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || gpdata->closed || !gpdata->istream)
		return;
	g_input_stream_read_async(gpdata->istream, gpdata->rbuf, sizeof(gpdata->rbuf),
				  G_PRIORITY_DEFAULT, gpdata->cancellable,
				  remmina_telnet_on_read_done, g_object_ref(gp));
}

/* Tear the connection down and tell the protocol widget. error == NULL means a clean remote close. */
static void remmina_telnet_disconnected(RemminaProtocolWidget *gp, const gchar *error)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || gpdata->closed)
		return;
	gpdata->closed = TRUE;

	if (gpdata->cancellable)
		g_cancellable_cancel(gpdata->cancellable);
	if (gpdata->connection) {
		g_io_stream_close(G_IO_STREAM(gpdata->connection), NULL, NULL);
		gpdata->istream = NULL;
		gpdata->ostream = NULL;
		g_clear_object(&gpdata->connection);
	}

	if (error && error[0]) {
		gchar *msg = g_strdup_printf(_("Telnet connection to %s:%d failed: %s"),
					     gpdata->host ? gpdata->host : "?", gpdata->port, error);
		remmina_telnet_feed_message(gpdata, msg);
		remmina_plugin_service->protocol_plugin_set_error(gp, "%s", msg);
		g_free(msg);
	} else {
		remmina_telnet_feed_message(gpdata, _("Connection closed by the remote host"));
	}
	remmina_plugin_service->protocol_plugin_signal_connection_closed(gp);
}

static void remmina_telnet_on_connected(GObject *source, GAsyncResult *res, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GError *error = NULL;
	GSocketConnection *conn;
	GSocket *socket;

	conn = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(source), res, &error);

	if (!gpdata || gpdata->closed) {
		if (conn)
			g_object_unref(conn);
		g_clear_error(&error);
		g_object_unref(gp);
		return;
	}

	if (!conn) {
		remmina_telnet_disconnected(gp, error ? error->message : _("unknown error"));
		g_clear_error(&error);
		g_object_unref(gp);
		return;
	}

	gpdata->connection = conn;
	gpdata->istream = g_io_stream_get_input_stream(G_IO_STREAM(conn));
	gpdata->ostream = g_io_stream_get_output_stream(G_IO_STREAM(conn));
	socket = g_socket_connection_get_socket(conn);
	if (socket) {
		g_socket_set_keepalive(socket, TRUE);
#if defined(IPPROTO_TCP) && defined(TCP_NODELAY)
		g_socket_set_option(socket, IPPROTO_TCP, TCP_NODELAY, 1, NULL);
#endif
	}

	gpdata->telnet = telnet_init(remmina_telnet_telopts, remmina_telnet_event_handler, 0, gp);
	if (!gpdata->telnet) {
		remmina_telnet_disconnected(gp, _("could not initialise the TELNET state machine"));
		g_object_unref(gp);
		return;
	}
	gpdata->connected = TRUE;
	remmina_telnet_feed_message(gpdata, _("Connected"));

	/* Start the usual client-side negotiation; the server may already have sent its own. */
	telnet_negotiate(gpdata->telnet, TELNET_WILL, TELNET_TELOPT_TTYPE);
	telnet_negotiate(gpdata->telnet, TELNET_WILL, TELNET_TELOPT_NAWS);
	telnet_negotiate(gpdata->telnet, TELNET_DO, TELNET_TELOPT_SGA);
	telnet_negotiate(gpdata->telnet, TELNET_DO, TELNET_TELOPT_ECHO);

	remmina_telnet_start_read(gp);
	remmina_plugin_service->protocol_plugin_signal_connection_opened(gp);
	gtk_widget_grab_focus(gpdata->vte);
	g_object_unref(gp);
}

/* Runs in a worker thread: starting an SSH tunnel may block for a while */
static void remmina_telnet_tunnel_task(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)source;
	gchar *hostport;

	hostport = remmina_plugin_service->protocol_plugin_start_direct_tunnel(gp, TELNET_DEFAULT_PORT, FALSE);
	if (hostport)
		g_task_return_pointer(task, hostport, g_free);
	else
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", _("could not start the SSH tunnel"));
}

static void remmina_telnet_tunnel_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)source;
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GError *error = NULL;
	gchar *hostport;
	gchar *msg;

	hostport = g_task_propagate_pointer(G_TASK(res), &error);
	if (!gpdata || gpdata->closed) {
		g_free(hostport);
		g_clear_error(&error);
		return;
	}
	if (!hostport) {
		remmina_telnet_disconnected(gp, error ? error->message : _("unknown error"));
		g_clear_error(&error);
		return;
	}

	g_free(gpdata->host);
	gpdata->host = NULL;
	remmina_plugin_service->get_server_port(hostport, TELNET_DEFAULT_PORT, &gpdata->host, &gpdata->port);
	g_free(hostport);

	msg = g_strdup_printf(_("Connecting to %s:%d…"), gpdata->host, gpdata->port);
	remmina_telnet_feed_message(gpdata, msg);
	g_free(msg);

	gpdata->client = g_socket_client_new();
	g_socket_client_set_timeout(gpdata->client, 30);
	g_socket_client_connect_to_host_async(gpdata->client, gpdata->host, (guint16)gpdata->port,
					      gpdata->cancellable, remmina_telnet_on_connected, g_object_ref(gp));
}

/* ------------------------------------------------------------------------- */
/* VTE callbacks                                                               */
/* ------------------------------------------------------------------------- */

static void remmina_telnet_on_vte_commit(VteTerminal *vte, gchar *text, guint size, RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	guint i;

	if (!gpdata || gpdata->closed || !gpdata->telnet || !text || size == 0)
		return;

	telnet_send_text(gpdata->telnet, text, size);

	/* Old style line-mode servers do not echo: show what was typed locally */
	if (!gpdata->remote_echo && !gpdata->no_local_echo) {
		for (i = 0; i < size; i++) {
			if (text[i] == '\r')
				vte_terminal_feed(vte, "\r\n", 2);
			else if (text[i] == 0x7f || text[i] == '\b')
				vte_terminal_feed(vte, "\b \b", 3);
			else
				vte_terminal_feed(vte, &text[i], 1);
		}
	}
}

static void remmina_telnet_on_vte_size_allocate(GtkWidget *widget, GtkAllocation *alloc, RemminaProtocolWidget *gp)
{
	if (!gtk_widget_get_mapped(widget))
		return;
	remmina_telnet_send_naws(gp, FALSE);
}

static gboolean remmina_telnet_on_focus_in(GtkWidget *widget, GdkEventFocus *event, RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (gpdata && gpdata->vte)
		gtk_widget_grab_focus(gpdata->vte);
	return TRUE;
}

static gboolean remmina_telnet_on_key_press(GtkWidget *widget, GdkEventKey *event, RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	guint keyval;

	if (!gpdata || !gpdata->vte)
		return FALSE;

	/* Ctrl+Shift+C / Ctrl+Shift+V like most terminal emulators */
	if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
		keyval = gdk_keyval_to_upper(event->keyval);
		if (keyval == GDK_KEY_C) {
			remmina_plugin_ssh_vte_copy_clipboard(NULL, gpdata->vte);
			return TRUE;
		}
		if (keyval == GDK_KEY_V) {
			remmina_plugin_ssh_vte_paste_clipboard(NULL, gpdata->vte);
			return TRUE;
		}
	}
	return FALSE;
}

static void remmina_telnet_popup_ui(RemminaProtocolWidget *gp)
{
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *select_all = gtk_menu_item_new_with_label(_("Select All (host+A)"));
	GtkWidget *copy = gtk_menu_item_new_with_label(_("Copy (host+C)"));
	GtkWidget *paste = gtk_menu_item_new_with_label(_("Paste (host+V)"));
	GtkWidget *font_incr = gtk_menu_item_new_with_label(_("Increase font size (host+Page Up)"));
	GtkWidget *font_decr = gtk_menu_item_new_with_label(_("Decrease font size (host+Page Down)"));

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_all);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), font_incr);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), font_decr);

	g_signal_connect(G_OBJECT(gpdata->vte), "button_press_event", G_CALLBACK(remmina_ssh_plugin_popup_menu), menu);
	g_signal_connect(G_OBJECT(select_all), "activate", G_CALLBACK(remmina_plugin_ssh_vte_select_all), gpdata->vte);
	g_signal_connect(G_OBJECT(copy), "activate", G_CALLBACK(remmina_plugin_ssh_vte_copy_clipboard), gpdata->vte);
	g_signal_connect(G_OBJECT(paste), "activate", G_CALLBACK(remmina_plugin_ssh_vte_paste_clipboard), gpdata->vte);
	g_signal_connect(G_OBJECT(font_incr), "activate", G_CALLBACK(remmina_plugin_ssh_vte_increase_font), gpdata->vte);
	g_signal_connect(G_OBJECT(font_decr), "activate", G_CALLBACK(remmina_plugin_ssh_vte_decrease_font), gpdata->vte);
	gtk_widget_show_all(menu);
}

/* ------------------------------------------------------------------------- */
/* Protocol plugin entry points                                               */
/* ------------------------------------------------------------------------- */

static void remmina_plugin_telnet_init(RemminaProtocolWidget *gp)
{
	TRACE_CALL(__func__);
	RemminaPluginTelnetData *gpdata;
	RemminaFile *remminafile;
	GtkWidget *hbox;
	GtkWidget *vte;
	const gchar *charset;
	const gchar *s;

	gpdata = g_new0(RemminaPluginTelnetData, 1);
	g_object_set_data_full(G_OBJECT(gp), "plugin-data", gpdata, remmina_telnet_data_free);
	gpdata->wqueue = g_byte_array_new();
	gpdata->recent = g_string_new(NULL);
	gpdata->cancellable = g_cancellable_new();
	gpdata->port = TELNET_DEFAULT_PORT;

	remminafile = remmina_plugin_service->protocol_plugin_get_file(gp);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show(hbox);
	gtk_container_add(GTK_CONTAINER(gp), hbox);
	g_signal_connect(G_OBJECT(hbox), "focus-in-event", G_CALLBACK(remmina_telnet_on_focus_in), gp);

	/* Terminal widget with the same colour scheme / font / scrollback handling as SSH */
	vte = remmina_plugin_ssh_vte_new(gp);
	gtk_widget_show(vte);
	gtk_box_pack_start(GTK_BOX(hbox), vte, TRUE, TRUE, 0);
	gpdata->vte = vte;

	charset = remmina_plugin_service->file_get_string(remminafile, "telnet_charset");
	if (charset && charset[0] && g_ascii_strcasecmp(charset, "UTF-8") != 0) {
		GError *err = NULL;
		if (!vte_terminal_set_encoding(VTE_TERMINAL(vte), charset, &err)) {
			REMMINA_WARNING("Cannot set terminal encoding %s: %s", charset, err ? err->message : "");
			g_clear_error(&err);
		}
	}

	s = remmina_plugin_service->file_get_string(remminafile, "username");
	gpdata->username = (s && s[0]) ? g_strdup(s) : NULL;
	s = remmina_plugin_service->file_get_string(remminafile, "password");
	gpdata->password = (s && s[0]) ? g_strdup(s) : NULL;
	gpdata->no_local_echo = remmina_plugin_service->file_get_int(remminafile, "telnet_no_local_echo", FALSE);

	remmina_plugin_service->protocol_plugin_register_hostkey(gp, vte);
	g_signal_connect(G_OBJECT(vte), "key-press-event", G_CALLBACK(remmina_telnet_on_key_press), gp);
	g_signal_connect(G_OBJECT(vte), "commit", G_CALLBACK(remmina_telnet_on_vte_commit), gp);
	g_signal_connect(G_OBJECT(vte), "size-allocate", G_CALLBACK(remmina_telnet_on_vte_size_allocate), gp);
	remmina_telnet_popup_ui(gp);
}

static gboolean remmina_plugin_telnet_open_connection(RemminaProtocolWidget *gp)
{
	TRACE_CALL(__func__);
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);
	GTask *task;

	remmina_plugin_service->protocol_plugin_set_expand(gp, TRUE);
	remmina_plugin_service->protocol_plugin_set_width(gp, 640);
	remmina_plugin_service->protocol_plugin_set_height(gp, 480);

	gpdata->closed = FALSE;
	gpdata->connected = FALSE;
	gpdata->user_sent = FALSE;
	gpdata->pass_sent = FALSE;
	gpdata->remote_echo = FALSE;
	gpdata->naws_enabled = FALSE;
	gpdata->last_cols = gpdata->last_rows = 0;
	g_string_set_size(gpdata->recent, 0);

	/* Resolve the real target (possibly through an SSH tunnel) off the main thread, then connect asynchronously */
	task = g_task_new(gp, gpdata->cancellable, remmina_telnet_tunnel_done, NULL);
	g_task_run_in_thread(task, remmina_telnet_tunnel_task);
	g_object_unref(task);
	return TRUE;
}

static gboolean remmina_plugin_telnet_close_connection(RemminaProtocolWidget *gp)
{
	TRACE_CALL(__func__);
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata)
		return FALSE;
	if (!gpdata->closed) {
		gpdata->closed = TRUE;
		if (gpdata->cancellable)
			g_cancellable_cancel(gpdata->cancellable);
		if (gpdata->connection) {
			g_io_stream_close(G_IO_STREAM(gpdata->connection), NULL, NULL);
			gpdata->istream = NULL;
			gpdata->ostream = NULL;
			g_clear_object(&gpdata->connection);
		}
		remmina_plugin_service->protocol_plugin_signal_connection_closed(gp);
	}
	return FALSE;
}

static gboolean remmina_plugin_telnet_query_feature(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature)
{
	TRACE_CALL(__func__);
	return TRUE;
}

static void remmina_plugin_telnet_call_feature(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature)
{
	TRACE_CALL(__func__);
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || !gpdata->vte)
		return;
	switch (feature->id) {
	case REMMINA_PLUGIN_SSH_FEATURE_TOOL_COPY:
		remmina_plugin_ssh_vte_copy_clipboard(NULL, gpdata->vte);
		break;
	case REMMINA_PLUGIN_SSH_FEATURE_TOOL_PASTE:
		remmina_plugin_ssh_vte_paste_clipboard(NULL, gpdata->vte);
		break;
	case REMMINA_PLUGIN_SSH_FEATURE_TOOL_SELECT_ALL:
		remmina_plugin_ssh_vte_select_all(NULL, gpdata->vte);
		break;
	case REMMINA_PLUGIN_SSH_FEATURE_TOOL_INCREASE_FONT:
		remmina_plugin_ssh_vte_increase_font(NULL, gpdata->vte);
		break;
	case REMMINA_PLUGIN_SSH_FEATURE_TOOL_DECREASE_FONT:
		remmina_plugin_ssh_vte_decrease_font(NULL, gpdata->vte);
		break;
	default:
		break;
	}
}

static void remmina_plugin_telnet_keystroke(RemminaProtocolWidget *gp, const guint keystrokes[], const gint keylen)
{
	TRACE_CALL(__func__);
	RemminaPluginTelnetData *gpdata = GET_PLUGIN_DATA(gp);

	if (!gpdata || !gpdata->vte)
		return;
	remmina_plugin_service->protocol_plugin_send_keys_signals(gpdata->vte, keystrokes, keylen, GDK_KEY_PRESS | GDK_KEY_RELEASE);
}

/* ------------------------------------------------------------------------- */
/* Plugin description                                                          */
/* ------------------------------------------------------------------------- */

static gpointer telnet_charset_list[] =
{
	"UTF-8",       "UTF-8",
	"ISO-8859-1",  "ISO-8859-1 (Latin-1)",
	"ISO-8859-15", "ISO-8859-15 (Latin-9)",
	"CP1251",      "Windows-1251 (Cyrillic)",
	"KOI8-R",      "KOI8-R (Cyrillic)",
	"CP866",       "CP866 (DOS Cyrillic)",
	"CP1252",      "Windows-1252 (Western)",
	"ISO-8859-2",  "ISO-8859-2 (Central European)",
	"CP1250",      "Windows-1250 (Central European)",
	"EUC-JP",      "EUC-JP (Japanese)",
	"SHIFT_JIS",   "Shift_JIS (Japanese)",
	"GBK",         "GBK (Chinese)",
	"BIG5",        "Big5 (Chinese)",
	"EUC-KR",      "EUC-KR (Korean)",
	NULL
};

static RemminaProtocolFeature remmina_plugin_telnet_features[] =
{
	{ REMMINA_PROTOCOL_FEATURE_TYPE_TOOL, REMMINA_PLUGIN_SSH_FEATURE_TOOL_COPY,	    N_("Copy"),		      N_("_Copy"),		 NULL },
	{ REMMINA_PROTOCOL_FEATURE_TYPE_TOOL, REMMINA_PLUGIN_SSH_FEATURE_TOOL_PASTE,	    N_("Paste"),	      N_("_Paste"),		 NULL },
	{ REMMINA_PROTOCOL_FEATURE_TYPE_TOOL, REMMINA_PLUGIN_SSH_FEATURE_TOOL_SELECT_ALL,   N_("Select all"),	      N_("_Select all"),	 NULL },
	{ REMMINA_PROTOCOL_FEATURE_TYPE_TOOL, REMMINA_PLUGIN_SSH_FEATURE_TOOL_INCREASE_FONT, N_("Increase font size"), N_("_Increase font size"), NULL },
	{ REMMINA_PROTOCOL_FEATURE_TYPE_TOOL, REMMINA_PLUGIN_SSH_FEATURE_TOOL_DECREASE_FONT, N_("Decrease font size"), N_("_Decrease font size"), NULL },
	{ REMMINA_PROTOCOL_FEATURE_TYPE_END,  0,					    NULL,		      NULL,			 NULL }
};

static const RemminaProtocolSetting remmina_telnet_basic_settings[] =
{
	{ REMMINA_PROTOCOL_SETTING_TYPE_SERVER,	  "server",   NULL,		    FALSE, "_telnet._tcp", NULL,									  NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_TEXT,	  "username", N_("Username"),	    FALSE, NULL,	   N_("Sent automatically when the server prints a login prompt"),	  NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_PASSWORD, "password", N_("User password"),  FALSE, NULL,	   N_("Sent automatically when the server prints a password prompt"), NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_END,	  NULL,	      NULL,		    FALSE, NULL,	   NULL,									  NULL, NULL }
};

/* Entry 0 (colour scheme list) is replaced at registration time with the dynamic palette list shared with SSH */
static RemminaProtocolSetting remmina_telnet_advanced_settings[] =
{
	{ REMMINA_PROTOCOL_SETTING_TYPE_SELECT, "ssh_color_scheme",	 N_("Terminal colour scheme"), FALSE, NULL,		   NULL,								     NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_SELECT, "telnet_charset",	 N_("Character set"),	       FALSE, telnet_charset_list, NULL,								     NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_CHECK,	"telnet_no_local_echo",	 N_("Never echo locally"),     TRUE,  NULL,		   N_("By default typed text is echoed locally only when the server does not echo it"), NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_CHECK,	"audiblebell",		 N_("Audible terminal bell"),  TRUE,  NULL,		   NULL,								     NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_CHECK,	"disablepasswordstoring", N_("Don't remember passwords"), TRUE, NULL,		   NULL,								     NULL, NULL },
	{ REMMINA_PROTOCOL_SETTING_TYPE_END,	NULL,			 NULL,			       FALSE, NULL,		   NULL,								     NULL, NULL }
};

static RemminaProtocolPlugin remmina_plugin_telnet =
{
	REMMINA_PLUGIN_TYPE_PROTOCOL,                   /**< Type */
	"TELNET",                                       /**< Name */
	N_("TELNET - Terminal (unencrypted)"),          /**< Description */
	GETTEXT_PACKAGE,                                /**< Translation domain */
	VERSION,                                        /**< Version number */
	REMMINA_APP_ID "-telnet-symbolic",              /**< Icon for normal connection */
	REMMINA_APP_ID "-telnet-symbolic",              /**< Icon for SSH connection */
	remmina_telnet_basic_settings,                  /**< Array for basic settings */
	remmina_telnet_advanced_settings,               /**< Array for advanced settings */
	REMMINA_PROTOCOL_SSH_SETTING_TUNNEL,            /**< SSH settings type */
	remmina_plugin_telnet_features,                 /**< Array for available features */
	remmina_plugin_telnet_init,                     /**< Plugin initialization */
	remmina_plugin_telnet_open_connection,          /**< Plugin open connection */
	remmina_plugin_telnet_close_connection,         /**< Plugin close connection */
	remmina_plugin_telnet_query_feature,            /**< Query for available features */
	remmina_plugin_telnet_call_feature,             /**< Call a feature */
	remmina_plugin_telnet_keystroke,                /**< Send a keystroke */
	NULL,                                           /**< No screenshot support available */
	NULL,                                           /**< RCW map event */
	NULL                                            /**< RCW unmap event */
};

void remmina_telnet_plugin_register(void)
{
	TRACE_CALL(__func__);

	remmina_plugin_service = &remmina_plugin_manager_service;

	remmina_plugin_telnet_features[0].opt3 = GUINT_TO_POINTER(remmina_pref.vte_shortcutkey_copy);
	remmina_plugin_telnet_features[1].opt3 = GUINT_TO_POINTER(remmina_pref.vte_shortcutkey_paste);
	remmina_plugin_telnet_features[2].opt3 = GUINT_TO_POINTER(remmina_pref.vte_shortcutkey_select_all);
	remmina_plugin_telnet_features[3].opt3 = GUINT_TO_POINTER(remmina_pref.vte_shortcutkey_increase_font);
	remmina_plugin_telnet_features[4].opt3 = GUINT_TO_POINTER(remmina_pref.vte_shortcutkey_decrease_font);

	/* Same colour scheme choices as the SSH plugin (built-in schemes + .colors files) */
	remmina_telnet_advanced_settings[0].opt1 = remmina_ssh_plugin_get_terminal_palette_list();

	remmina_plugin_service->register_plugin((RemminaPlugin *)&remmina_plugin_telnet);
}

#else /* !HAVE_LIBVTE */

void remmina_telnet_plugin_register(void)
{
	TRACE_CALL(__func__);
}

#endif
