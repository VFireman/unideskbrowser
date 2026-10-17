/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009-2011 Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
 * Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
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


#include "config.h"

#ifdef GDK_WINDOWING_X11
#include <cairo/cairo-xlib.h>
#else
#include <cairo/cairo.h>
#endif
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "remmina.h"
#include "remmina_main.h"
#include "rcw.h"
#include "remmina_applet_menu_item.h"
#include "remmina_applet_menu.h"
#include "remmina_file.h"
#include "remmina_file_editor.h"
#include "remmina_file_manager.h"
#include "remmina_log.h"
#include "remmina_message_panel.h"
#include "remmina_ext_exec.h"
#include "remmina_plugin_manager.h"
#include "remmina_pref.h"
#include "remmina_theme.h"
#include "remmina_protocol_widget.h"
#include "remmina_public.h"
#include "remmina_scrolled_viewport.h"
#include "remmina_unlock.h"
#include "remmina_utils.h"
#include "remmina_widget_pool.h"
#include "remmina_session.h"
#include "remmina_icon.h"
#include "remmina_db.h"
#include "remmina_ftp_client.h"
#include "rcw_platform.h"
#include "remmina/remmina_trace_calls.h"

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif


#define DEBUG_KB_GRABBING 0
#include "remmina_exec.h"

gchar *remmina_pref_file;
RemminaPref remmina_pref;

G_DEFINE_TYPE(RemminaConnectionWindow, rcw, GTK_TYPE_WINDOW)

#define MOTION_TIME 100

#define IS_WAYLAND(widget) (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(widget))))

/* default timeout used to hide the floating toolbar when switching profile */
#define TB_HIDE_TIME_TIME 1500

#define FULL_SCREEN_TARGET_MONITOR_UNDEFINED -1

struct _RemminaConnectionWindowPriv {
	GtkNotebook *					notebook;
	GtkWidget *					floating_toolbar_widget;
	GtkWidget *					overlay;
	GtkWidget *					revealer;
	GtkWidget *					overlay_ftb_overlay;
	GtkWidget *					overlay_ftb_fr;
	gint						ftb_move_horiz;
	gint						ftb_move_vert;

	GPtrArray * 				multimon_areas;
	gint					    multimon_main_monitor;

	GtkWidget *					floating_toolbar_label;
	gdouble						floating_toolbar_opacity;

	/* Various delayed and timer event source ids */
	guint						acs_eventsourceid;              // timeout
	guint						spf_eventsourceid;              // idle
	guint						delayed_grab_eventsourceid;
	guint						grab_retry_eventsourceid;       // timeout
	guint						ftb_hide_eventsource;           // timeout
	guint						ftb_show_eventsource;           // timeout
	guint						tar_eventsource;                // timeout
	guint						hidetb_eventsource;             // timeout
	guint						dwp_eventsourceid;              // timeout

	GtkWidget *					toolbar;
	GtkWidget *					grid;

	/* Toolitems that need to be handled */
	GtkToolItem *					toolitem_menu;
	GtkToolItem *					toolitem_autofit;
	GtkToolItem *					toolitem_fullscreen;
	GtkToolItem *					toolitem_switch_page;
	GtkToolItem *					toolitem_dynres;
	GtkToolItem *					toolitem_scale;
	GtkToolItem *					toolitem_viewonly;
	GtkToolItem *					toolitem_grab;
	GtkToolItem *					toolitem_invert_scroll;
	GtkToolItem *					toolitem_multimon;
	GtkToolItem *					toolitem_preferences;
	GtkToolItem *					toolitem_tools;
	GtkToolItem *					toolitem_creds;
	GtkToolItem *					toolitem_new;
	GtkToolItem *					toolitem_duplicate;
	GtkToolItem *					toolitem_screenshot;
	GtkToolItem *					toolitem_edit_pref;
	GtkWidget *					fullscreen_option_button;
	GtkWidget *					fullscreen_scaler_button;
	GtkWidget *					scaler_option_button;

	GtkWidget *					pin_button;
	gboolean					pin_down;

	gboolean					sticky;

	/* Flag to turn off toolbar signal handling when toolbar is
	 * reconfiguring, usually due to a tab switch */
	gboolean					toolbar_is_reconfiguring;

	/* This is the current view mode, i.e. VIEWPORT_FULLSCREEN_MODE,
	 * as saved on the "viwemode" profile preference file */
	gint						view_mode;
	gboolean					multi_mon;

	/* Status variables used when in fullscreen mode. Needed
	 * to restore a fullscreen mode after coming from scrolled */
	gint						fss_view_mode;
	/* Status variables used when in scrolled window mode. Needed
	 * to restore a scrolled window mode after coming from fullscreen */
	gint						ss_width, ss_height;
	gboolean					ss_maximized;

	gint						grab_realize_source_id;
	gboolean					grab_pending_status;
	gboolean					grab_current_status;

	gboolean					hostkey_activated;
	gboolean					hostkey_used;

	RemminaConnectionWindowOnDeleteConfirmMode	on_delete_confirm_mode;

	/* Unidesk: application windows with browser-like tabs */
	gboolean					is_main;		/* an application window (not a fullscreen one) */
	gboolean					quit_requested;		/* closing: quit when the last tab is gone */
	gboolean					closing_window;		/* closing this window only (others stay open) */
	guint						quit_watchdog_eventsource;
	GtkWidget *					headerbar;
	GtkWidget *					sidebar_toggle;		/* GtkToggleButton in the header bar: shows/hides the connection toolbar */
	GtkWidget *					home_page;		/* the "Connections" tab (list of profiles) */
	GtkWidget *					sidebar_box;		/* content of the home page: the connection list when current */
	GtkWidget *					home_placeholder;	/* in the home page while the list is in another window */
	GtkWidget *					menu_slot;		/* end of the header bar: the main menu when current */
	GtkWidget *					content_stack;		/* "tabs" (notebook) / "empty" placeholder */
	GtkWidget *					empty_box;
	/* The tabs live in the header bar: one toggle per notebook page and the "new connection" button last */
	GtkWidget *					tabstrip_scroller;	/* GtkScrolledWindow, the header bar custom title */
	GtkWidget *					tabstrip;		/* GtkBox with the tab toggles */
	GtkWidget *					tabstrip_new_button;
	gboolean					tabstrip_updating;	/* TRUE while the strip is being rebuilt: ignore toggles */
	/* Fullscreen: an application window lends its tabs to a fullscreen window and hides */
	RemminaConnectionWindow *			fullscreen_win;		/* application window: the one holding its tabs */
	RemminaConnectionWindow *			origin_win;		/* fullscreen window: whose tabs it holds */
	/* Application window: what it holds, kept in sessions.db (remmina_session.c) */
	RemminaSession *				session;
	guint						session_save_id;	/* a change waits to be written */
	gboolean					session_frozen;		/* closing: tabs going away do not change it */
	gboolean					session_stored;		/* sessions.db has it */
	guint						empty_check_id;		/* its last tab left: does it close? */
};

/* The application windows, in the order they were opened, and the current one: the last that had
 * the focus. New connections, tool tabs and dialogs go to the current window; it also holds the
 * connection list (remmina_main.c), which follows the focus. */
static GList *rcw_windows = NULL;
static RemminaConnectionWindow *rcw_main = NULL;
/* The application is quitting: every window closes, none is "closed alone" */
static gboolean rcw_app_quitting = FALSE;
/* remmina_exec_exitremmina() runs: it closes the windows itself and quits */
static gboolean rcw_app_exiting = FALSE;

typedef struct _RemminaConnectionObject {
	RemminaConnectionWindow *	cnnwin;
	RemminaFile *			remmina_file;

	GtkWidget *			proto;
	GtkWidget *			aspectframe;
	GtkWidget *			viewport;

	GtkWidget *			scrolled_container;
	GtkWidget *			tab_page;

	gboolean			plugin_can_scale;

	gboolean			connected;
	gboolean			dynres_unlocked;

	gulong				deferred_open_size_allocate_handler;
} RemminaConnectionObject;

G_DEFINE_TYPE(MultimonWindow, multimon_window, GTK_TYPE_WINDOW)

enum {
	TOOLBARPLACE_SIGNAL,
	LAST_SIGNAL
};

static guint rcw_signals[LAST_SIGNAL] =
{ 0 };

static RemminaConnectionWindow *rcw_create_main(void);
static void rcw_set_current(RemminaConnectionWindow *cnnwin);
static void rcw_session_changed(RemminaConnectionWindow *cnnwin);
static void rcw_session_freeze(RemminaConnectionWindow *cnnwin);
static void rcw_session_freeze_all(void);
static void rcw_sessions_refresh_all(void);
static void rcw_apply_accent(RemminaConnectionWindow *cnnwin);
static void rcw_show_accent_popover(GtkWidget *relative_to, RemminaConnectionWindow *cnnwin);
static RemminaConnectionWindow *rcw_create_fullscreen(GtkWindow *old, gint view_mode);
static void rcw_update_empty_state(RemminaConnectionWindow *cnnwin);
static void rcw_switch_viewmode(RemminaConnectionWindow *cnnwin, int newmode);
static gboolean rcw_page_is_tool(GtkWidget *page);
static const gchar *rcw_tool_page_title(GtkWidget *page);
static gboolean rcw_widget_in_sidebar(RemminaConnectionWindow *cnnwin, GtkWidget *w);
static gboolean rcw_on_switch_tool_page_idle(gpointer user_data);
static GtkWidget *rcw_create_empty_state(RemminaConnectionWindow *cnnwin);
static void rcw_on_set_focus(GtkWindow *window, GtkWidget *widget, gpointer data);
static void rcw_on_sidebar_toggle(GtkToggleButton *btn, RemminaConnectionWindow *cnnwin);
static GtkWidget *rcw_tabstrip_new(RemminaConnectionWindow *cnnwin);
static void rcw_tabstrip_rebuild(RemminaConnectionWindow *cnnwin);
static void rcw_tabstrip_sync_current(RemminaConnectionWindow *cnnwin, GtkWidget *page);
static gboolean rcw_accel_close_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data);
static gboolean rcw_accel_new_window(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data);
static gboolean rcw_accel_home_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data);
static gboolean rcw_accel_reopen_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data);
static void rcw_remember_closed_tab(GtkWidget *page);
static gboolean rcw_keyboard_is_grabbed(RemminaConnectionWindow *cnnwin);
static void rcw_tabstrip_on_close_clicked(GtkButton *button, gpointer user_data);
static void rcw_set_toolbar_visibility(RemminaConnectionWindow *cnnwin);
static gboolean rcw_hostkey_func(RemminaProtocolWidget *gp, guint keyval, gboolean release);
static GtkWidget *rco_create_tab_page(RemminaConnectionObject *cnnobj);
static GtkWidget *rco_create_tab_label(RemminaConnectionObject *cnnobj);
static GtkWidget *nb_find_page_by_cnnobj(GtkNotebook *notebook, RemminaConnectionObject *cnnobj);
static void rco_change_scalemode(RemminaConnectionObject *cnnobj, gboolean bdyn, gboolean bscale);

void rcw_grab_focus(RemminaConnectionWindow *cnnwin);
static GtkWidget *rcw_create_toolbar(RemminaConnectionWindow *cnnwin, gint mode, gboolean is_floating);
static void rcw_place_toolbar(GtkToolbar *toolbar, GtkGrid *grid, GtkWidget *sibling, int toolbar_placement);
static void rco_update_toolbar(RemminaConnectionObject *cnnobj);
static void rcw_keyboard_grab_request(RemminaConnectionObject *cnnobj, gboolean want_grab);
static void rcw_keyboard_grab(RemminaConnectionWindow *cnnwin);
static GtkWidget *rcw_append_new_page(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj);

static void rcw_ftb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data);

static void rcw_create_widget_areas(RemminaConnectionObject*cnnobj);
static GdkWindow* rcw_widget_active_area(RemminaConnectionWindow*cnnwin);
static void rcw_show_widget_areas(RemminaConnectionWindow*priv);
static void rcw_iconify_widget_areas(RemminaConnectionWindow*priv);
static void rcw_restore_widget_areas(RemminaConnectionWindow*priv);
static void rcw_dispose_widget_areas(RemminaConnectionWindowPriv*priv);

static const GtkTargetEntry dnd_targets_ftb[] =
{
	{
		(char *)"text/x-remmina-ftb",
		GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
		0
	},
};

static const GtkTargetEntry dnd_targets_tb[] =
{
	{
		(char *)"text/x-remmina-tb",
		GTK_TARGET_SAME_APP,
		0
	},
};

static void rcw_class_init(RemminaConnectionWindowClass *klass)
{
	TRACE_CALL(__func__);
	GtkCssProvider *provider;

	provider = gtk_css_provider_new();

	/* It’s important to remove padding, border and shadow from GtkViewport or
	 * we will never know its internal area size, because GtkViweport::viewport_get_view_allocation,
	 * which returns the internal size of the GtkViewport, is private and we cannot access it */

#if GTK_CHECK_VERSION(3, 14, 0)
	gtk_css_provider_load_from_data(provider,
					"#remmina-cw-viewport, #remmina-cw-aspectframe {\n"
					"  padding:0;\n"
					"  border:0;\n"
					"  background-color: black;\n"
					"}\n"
					"GtkDrawingArea {\n"
					"}\n"
					"GtkToolbar {\n"
					"  -GtkWidget-window-dragging: 0;\n"
					"}\n"
					"#remmina-connection-window-fullscreen {\n"
					"  border-color: black;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-small-button {\n"
					"  outline-offset: 0;\n"
					"  outline-width: 0;\n"
					"  padding: 0;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-pin-button {\n"
					"  outline-offset: 0;\n"
					"  outline-width: 0;\n"
					"  padding: 2px;\n"
					"  border: 0;\n"
					"}\n"
					/* Unidesk: browser-like tabs inside the header bar */
					"headerbar .unidesk-tabstrip, headerbar .unidesk-tabstrip viewport {\n"
					"  background: none;\n"
					"  border: none;\n"
					"  padding: 0;\n"
					"  margin: 0;\n"
					"}\n"
					/* a tab is a box [toggle][close]: the box carries the "current" highlight,
					 * the buttons inside are flat (nested buttons would not get the clicks) */
					".unidesk-tab-box {\n"
					"  border-radius: 0;\n"
					"  background: none;\n"
					"}\n"
					".unidesk-tab-box.current {\n"
					"  background: alpha(@theme_fg_color, 0.12);\n"
					"  box-shadow: inset 0 -3px 0 0 @accent_color;\n"
					"}\n"
					"button.unidesk-tab, button.unidesk-tab-close, button.unidesk-tab-new {\n"
					"  border-radius: 0;\n"
					"  border: none;\n"
					"  box-shadow: none;\n"
					"  background: none;\n"
					"  padding: 0 8px;\n"
					"  margin: 0;\n"
					"  outline-offset: -4px;\n"
					"}\n"
					"button.unidesk-tab-close {\n"
					"  padding: 0 6px 0 2px;\n"
					"}\n"
					"button.unidesk-tab:checked, button.unidesk-tab:active, button.unidesk-tab:checked:hover {\n"
					"  background: none;\n"
					"  box-shadow: none;\n"
					"}\n"
					"button.unidesk-tab:hover, button.unidesk-tab-close:hover, button.unidesk-tab-new:hover {\n"
					"  background: alpha(@theme_fg_color, 0.08);\n"
					"}\n"
					"#remmina-tab-page {\n"
					"  background-color: black;\n"
					"}\n"
					".remmina-active-terminal-pane {\n"
					"  border: 1px solid @accent_color;\n"
					"}\n"
					"#remmina-scrolled-container {\n"
					"}\n"
					"#remmina-scrolled-container.undershoot {\n"
					"  background: none;\n"
					"}\n"
					"#remmina-tab-page {\n"
					"}\n"
					"#ftbbox-upper {\n"
					"  background-color: @theme_bg_color;\n"
					"  color: @theme_fg_color;\n"
					"  border-color: @borders;\n"
					"  border-style: none solid solid solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftbbox-lower {\n"
					"  background-color: @theme_bg_color;\n"
					"  color: @theme_fg_color;\n"
					"  border-color: @borders;\n"
					"  border-style: solid solid none solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftb-handle {\n"
					"}\n"
					".message_panel {\n"
					"  border: 0px solid;\n"
					"  padding: 20px 20px 20px 20px;\n"
					"}\n"
					".message_panel entry {\n"
					"  background-image: none;\n"
					"  border-width: 4px;\n"
					"  border-radius: 8px;\n"
					"}\n"
					".message_panel .title_label {\n"
					"  font-size: 2em; \n"
					"}\n"
					, -1, NULL);

#else
	gtk_css_provider_load_from_data(provider,
					"#remmina-cw-viewport, #remmina-cw-aspectframe {\n"
					"  padding:0;\n"
					"  border:0;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-cw-message-panel {\n"
					"}\n"
					"GtkDrawingArea {\n"
					"}\n"
					"GtkToolbar {\n"
					"  -GtkWidget-window-dragging: 0;\n"
					"}\n"
					"#remmina-connection-window-fullscreen {\n"
					"  border-color: black;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-small-button {\n"
					"  -GtkWidget-focus-padding: 0;\n"
					"  -GtkWidget-focus-line-width: 0;\n"
					"  padding: 0;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-pin-button {\n"
					"  -GtkWidget-focus-padding: 0;\n"
					"  -GtkWidget-focus-line-width: 0;\n"
					"  padding: 2px;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-scrolled-container {\n"
					"}\n"
					"#remmina-scrolled-container.undershoot {\n"
					"  background: none\n"
					"}\n"
					"#remmina-tab-page {\n"
					"}\n"
					"#ftbbox-upper {\n"
					"  border-style: none solid solid solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftbbox-lower {\n"
					"  border-style: solid solid none solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftb-handle {\n"
					"}\n"

					, -1, NULL);
#endif

	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
						  GTK_STYLE_PROVIDER(provider),
						  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_object_unref(provider);

	/* Define a signal used to notify all rcws of toolbar move */
	rcw_signals[TOOLBARPLACE_SIGNAL] = g_signal_new("toolbar-place", G_TYPE_FROM_CLASS(klass),
							G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET(RemminaConnectionWindowClass, toolbar_place), NULL, NULL,
							g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static RemminaConnectionObject *rcw_get_cnnobj_at_page(RemminaConnectionWindow *cnnwin, gint npage)
{
	GtkWidget *po;
	RemminaConnectionObject *active_cnn;

	if (!cnnwin->priv->notebook)
		return NULL;
	po = gtk_notebook_get_nth_page(GTK_NOTEBOOK(cnnwin->priv->notebook), npage);
	if (!po)
		return NULL;
	active_cnn = g_object_get_data(G_OBJECT(po), "active-cnnobj");
	if (active_cnn){
		return active_cnn;
	}
	return g_object_get_data(G_OBJECT(po), "cnnobj");
}

void rcw_set_active_pane(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	if (!cnnwin || !cnnobj)
		return;

	GtkWidget *page = cnnobj->tab_page ? cnnobj->tab_page : (cnnwin->priv && cnnwin->priv->notebook ? nb_find_page_by_cnnobj(cnnwin->priv->notebook, cnnobj) : NULL);
	if (!page)
		return;

	RemminaConnectionObject *prev_active = g_object_get_data(G_OBJECT(page), "active-cnnobj");
	if (prev_active == cnnobj)
		return;

	g_object_set_data(G_OBJECT(page), "active-cnnobj", cnnobj);

	// Update CSS highlight 
	GList *list = g_object_get_data(G_OBJECT(page), "cnnobj-list");
	guint len = g_list_length(list);
	for (GList *l = list; l != NULL; l = l->next) {
		RemminaConnectionObject *co = (RemminaConnectionObject *)l->data;
		if (co && co->scrolled_container) {
			GtkStyleContext *ctx = gtk_widget_get_style_context(co->scrolled_container);
			if (co == cnnobj && len > 1) {
				gtk_style_context_add_class(ctx, "remmina-active-terminal-pane");
			} else {
				gtk_style_context_remove_class(ctx, "remmina-active-terminal-pane");
			}
		}
	}

	//Update toolbar for the newly active pane 
	rco_update_toolbar(cnnobj);
}

void rcw_set_active_pane_for_protocol_widget(RemminaProtocolWidget *gp)
{
	if (gp && gp->cnnobj && gp->cnnobj->cnnwin) {
		rcw_set_active_pane(gp->cnnobj->cnnwin, gp->cnnobj);
	}
}

static RemminaConnectionObject *rcw_get_visible_cnnobj(RemminaConnectionWindow *cnnwin)
{
	gint np;

	if (cnnwin != NULL && cnnwin->priv != NULL && cnnwin->priv->notebook != NULL) {
		np = gtk_notebook_get_current_page(GTK_NOTEBOOK(cnnwin->priv->notebook));
		if (np < 0)
			return NULL;
		return rcw_get_cnnobj_at_page(cnnwin, np);
	} else {
		return NULL;
	}
}

static RemminaScaleMode get_current_allowed_scale_mode(RemminaConnectionObject *cnnobj, gboolean *dynres_avail, gboolean *scale_avail)
{
	TRACE_CALL(__func__);
	RemminaScaleMode scalemode;
	gboolean plugin_has_dynres, plugin_can_scale, is_multimon;

	scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									  REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

	is_multimon = remmina_protocol_widget_get_multimon(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									 REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

	/* force switch scale mode when auto and unlocked */
	if (remmina_pref.start_dynres && plugin_has_dynres && cnnobj->dynres_unlocked && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
		rco_change_scalemode(cnnobj, TRUE, FALSE);
		scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}

	if (is_multimon && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES;
	}

	/* Forbid scalemode REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED when not possible */
	if (!plugin_can_scale && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE;

	if (scale_avail)
		*scale_avail = plugin_can_scale;
	if (dynres_avail)
		*dynres_avail = (plugin_has_dynres && cnnobj->dynres_unlocked);

	return scalemode;
}

static void rco_disconnect_current_page(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

	/* Disconnects the connection which is currently in view in the notebook */
	remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
}

static void rcw_kp_ungrab(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GdkDisplay *display;

#if GTK_CHECK_VERSION(3, 20, 0)
	GdkSeat *seat;
#else
	GdkDeviceManager *manager;
	GdkDevice *keyboard = NULL;
#endif


	if (cnnwin->priv->grab_retry_eventsourceid) {
		g_source_remove(cnnwin->priv->grab_retry_eventsourceid);
		cnnwin->priv->grab_retry_eventsourceid = 0;
	}
	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}
	if (cnnwin->priv->grab_realize_source_id) {
		g_source_remove(cnnwin->priv->grab_realize_source_id);
		cnnwin->priv->grab_realize_source_id = 0;
	}
	cnnwin->priv->grab_current_status = FALSE;
	cnnwin->priv->grab_pending_status = FALSE;

	display = gtk_widget_get_display(GTK_WIDGET(cnnwin));
#if GTK_CHECK_VERSION(3, 20, 0)
	seat = gdk_display_get_default_seat(display);
#else
	manager = gdk_display_get_device_manager(display);
	keyboard = gdk_device_manager_get_client_pointer(manager);
#endif

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- ungrabbing");
#endif

#if GTK_CHECK_VERSION(3, 20, 0)
	/* We can use gtk_seat_grab()/_ungrab() only after GTK 3.24 */
	gdk_seat_ungrab(seat);
#else
	if (keyboard != NULL) {
		if (gdk_device_get_source(keyboard) != GDK_SOURCE_KEYBOARD)
			keyboard = gdk_device_get_associated_device(keyboard);
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		gdk_device_ungrab(keyboard, GDK_CURRENT_TIME);
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
#endif
}

static gboolean rcw_keyboard_grab_retry(RemminaConnectionWindow*cnnwin)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: retry grab");
#endif
	rcw_keyboard_grab(cnnwin);
	cnnwin->priv->grab_retry_eventsourceid = 0;
	return G_SOURCE_REMOVE;
}

static void rcw_keyboard_grab(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GdkDisplay *display;

#if GTK_CHECK_VERSION(3, 20, 0)
	GdkSeat *seat;
#else
	GdkDeviceManager *manager;
#endif
	GdkGrabStatus ggs;
	GdkDevice *keyboard = NULL;

	display = gtk_widget_get_display(GTK_WIDGET(cnnwin));
#if GTK_CHECK_VERSION(3, 20, 0)
	seat = gdk_display_get_default_seat(display);
	keyboard = gdk_seat_get_pointer(seat);
#else
	manager = gdk_display_get_device_manager(display);
	keyboard = gdk_device_manager_get_client_pointer(manager);
#endif

	if (keyboard != NULL) {
		if (gdk_device_get_source(keyboard) != GDK_SOURCE_KEYBOARD)
			keyboard = gdk_device_get_associated_device(keyboard);

		GdkWindow*grab_window = rcw_widget_active_area(cnnwin);
		if (grab_window == NULL) {
			grab_window = gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(cnnwin)));
		}

#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: profile asks for grabbing, let’s try.");
#endif
		/* Up to GTK version 3.20 we can grab the keyboard with gdk_device_grab().
		 * in GTK 3.20 gdk_seat_grab() should be used instead of gdk_device_grab().
		 * There is a bug in GTK up to 3.22: When gdk_device_grab() fails
		 * the widget is hidden:
		 * https://gitlab.gnome.org/GNOME/gtk/commit/726ad5a5ae7c4f167e8dd454cd7c250821c400ab
		 * The bugfix will be released with GTK 3.24.
		 * Also please note that the newer gdk_seat_grab() is still calling gdk_device_grab().
		 *
		 * Warning: gdk_seat_grab() will call XGrabKeyboard() or XIGrabDevice()
		 * which in turn will generate a core X input event FocusOut and FocusIn
		 * but not Xinput2 events.
		 * In some cases, GTK is unable to neutralize FocusIn and FocusOut core
		 * events (ie: i3wm+Plasma with GDK_CORE_DEVICE_EVENTS=1 because detail=NotifyNonlinear
		 * instead of detail=NotifyAncestor/detail=NotifyInferior)
		 * Receiving a FocusOut event for Remmina at this time will cause an infinite loop.
		 * Therefore is important for GTK to use Xinput2 instead of core X events
		 * by unsetting GDK_CORE_DEVICE_EVENTS
		 */
#if GTK_CHECK_VERSION(3, 20, 0)
		ggs = gdk_seat_grab(seat, grab_window,
				    GDK_SEAT_CAPABILITY_KEYBOARD, TRUE, NULL, NULL, NULL, NULL);
#else
		ggs = gdk_device_grab(keyboard, grab_window, GDK_OWNERSHIP_WINDOW,
				      TRUE, GDK_KEY_PRESS | GDK_KEY_RELEASE, NULL, GDK_CURRENT_TIME);
#endif
		// in multimon, need 'insist'
		GdkGrabStatus success = cnnwin->priv->multi_mon ? GDK_GRAB_ALREADY_GRABBED : GDK_GRAB_SUCCESS;
		if (ggs != success) {
#if DEBUG_KB_GRABBING
			if (cnnwin->priv->multi_mon && ggs == GDK_GRAB_SUCCESS)
				REMMINA_DEBUG("DEBUG_KB_GRABBING: grab keyboard ok, %s", cnnwin->priv->grab_retry_eventsourceid == 0 ? "will be retried":"last try");
			else
				REMMINA_DEBUG("DEBUG_KB_GRABBING: grab keyboard failed: %d, will be retried", ggs);
#endif
			/* Reschedule grabbing in half a second if not already done */
			if (cnnwin->priv->grab_retry_eventsourceid == 0) {
				cnnwin->priv->grab_retry_eventsourceid = g_timeout_add(500, (GSourceFunc)rcw_keyboard_grab_retry, cnnwin);
			}
		} else {
#if DEBUG_KB_GRABBING
			REMMINA_DEBUG("DEBUG_KB_GRABBING: keyboard grabbed");
#endif			
			if (cnnwin->priv->grab_retry_eventsourceid != 0) {
				g_source_remove(cnnwin->priv->grab_retry_eventsourceid);
				cnnwin->priv->grab_retry_eventsourceid = 0;
			}
		}
	}
}


static gboolean rcw_keyboard_grab_realize(RemminaConnectionObject* cnnobj) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING");
#endif
	cnnobj->cnnwin->priv->grab_realize_source_id = 0;
	if (!cnnobj->cnnwin->priv->grab_pending_status) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: ungrabing");
#endif
		rcw_kp_ungrab(cnnobj->cnnwin);
		cnnobj->cnnwin->priv->grab_current_status = FALSE;
		return G_SOURCE_REMOVE;
	}
	
	if (!gtk_window_is_active(GTK_WINDOW(cnnobj->cnnwin)) && !rcw_widget_active_area(cnnobj->cnnwin)) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: canceled, no connection window is active for now");
#endif
		return G_SOURCE_REMOVE;
	}
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: call grab");
#endif
	rcw_keyboard_grab(cnnobj->cnnwin);
	cnnobj->cnnwin->priv->grab_current_status = TRUE;
	return G_SOURCE_REMOVE;
}

static void rcw_keyboard_grab_request(RemminaConnectionObject *cnnobj, gboolean want_grab)
{	
	if (!cnnobj || cnnobj->connected == FALSE)
		return;

	/* option must be activated grab */
	if (want_grab && remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE) == FALSE)
		return;
	
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: grab_wanted=%d, done=%d", want_grab, cnnobj->cnnwin->priv->grab_current_status);
#endif

	gboolean changed = cnnobj->cnnwin->priv->grab_pending_status != want_grab || cnnobj->cnnwin->priv->grab_pending_status != cnnobj->cnnwin->priv->grab_current_status;
	if (changed && cnnobj->cnnwin->priv->grab_realize_source_id != 0 && !want_grab) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: ignoring ungrab because grab already asked in this session");
#endif
		return;
	}
	cnnobj->cnnwin->priv->grab_pending_status  = want_grab;
	
	if (changed && cnnobj->cnnwin->priv->grab_realize_source_id == 0) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: launch deferred realize");
#endif
		cnnobj->cnnwin->priv->grab_realize_source_id = g_timeout_add(200, (GSourceFunc)rcw_keyboard_grab_realize, cnnobj);
	}
}

static void rcw_close_all_connections(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkNotebook *notebook;
	GtkWidget *w;
	RemminaConnectionObject *cnnobj;
	gint i, n;

	/* connection already closed */
	if (!priv)
		return;

	notebook = GTK_NOTEBOOK(priv->notebook);

	if (GTK_IS_WIDGET(notebook)) {
		n = gtk_notebook_get_n_pages(notebook);
		for (i = n - 1; i >= 0; i--) {
			w = gtk_notebook_get_nth_page(notebook, i);
			if (rcw_page_is_tool(w)) {
				/* Tool tab (editor, preferences…): destroying its window removes the tab */
				GtkWidget *toolwin = g_object_get_data(G_OBJECT(w), "tool-window");
				if (toolwin)
					gtk_widget_destroy(toolwin);
				continue;
			}
			GList *list = g_object_get_data(G_OBJECT(w), "cnnobj-list");
			if (list) {
				GList *copy = g_list_copy(list);
				for (GList *l = copy; l != NULL; l = l->next) {
					RemminaConnectionObject *co = (RemminaConnectionObject *)l->data;
					if (co && co->proto){
						remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(co->proto));
					}
				}
				g_list_free(copy);
			} else {
				cnnobj = (RemminaConnectionObject *)g_object_get_data(G_OBJECT(w), "cnnobj");
				if (cnnobj && cnnobj->proto){
					remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
				}
			}
		}
	}
}

/* TRUE while the window holds at least one connection tab (tool tabs and the
 * "Connections" tab do not count) */
static gboolean rcw_has_connection_pages(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	gint i, n;

	if (!priv || !priv->notebook || !GTK_IS_NOTEBOOK(priv->notebook))
		return FALSE;
	n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook));
	for (i = 0; i < n; i++)
		if (!rcw_page_is_tool(gtk_notebook_get_nth_page(GTK_NOTEBOOK(priv->notebook), i)))
			return TRUE;
	return FALSE;
}

/* Count the connections of a window that are still open (tool tabs do not count) */
static gint rcw_count_open_connections(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkNotebook *notebook;
	GtkWidget *w;
	gint i, n, nopen = 0;

	if (!priv || !priv->notebook)
		return 0;
	notebook = GTK_NOTEBOOK(priv->notebook);
	n = gtk_notebook_get_n_pages(notebook);
	for (i = 0; i < n; i++) {
		w = gtk_notebook_get_nth_page(notebook, i);
		if (rcw_page_is_tool(w))
			continue;
		GList *list = g_object_get_data(G_OBJECT(w), "cnnobj-list");
		if (list) {
			for (GList *l = list; l != NULL; l = l->next) {
				RemminaConnectionObject *co = (RemminaConnectionObject *)l->data;
				if (co && co->proto && !remmina_protocol_widget_is_closed((RemminaProtocolWidget *)co->proto))
					nopen++;
			}
		} else {
			RemminaConnectionObject *cnnobj = rcw_get_cnnobj_at_page(cnnwin, i);
			if (cnnobj && cnnobj->proto && !remmina_protocol_widget_is_closed((RemminaProtocolWidget *)cnnobj->proto))
				nopen++;
		}
	}
	return nopen;
}

static gboolean rcw_quit_idle(gpointer data)
{
	TRACE_CALL(__func__);
	remmina_exec_exitremmina();
	return G_SOURCE_REMOVE;
}

/* A window closing alone: a plugin did not emit "disconnect" after the close request */
static gboolean rcw_close_window_watchdog(gpointer data)
{
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)data;

	if (cnnwin && cnnwin->priv) {
		cnnwin->priv->quit_watchdog_eventsource = 0;
		gtk_widget_destroy(GTK_WIDGET(cnnwin));
	}
	return G_SOURCE_REMOVE;
}

/* Fired when a plugin does not emit "disconnect" after a close request: quit anyway */
static gboolean rcw_quit_watchdog(gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)data;

	if (cnnwin && cnnwin->priv)
		cnnwin->priv->quit_watchdog_eventsource = 0;
	remmina_exec_exitremmina();
	return G_SOURCE_REMOVE;
}

gboolean rcw_delete(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *dialog;
	gint i, nopen;

	/* connection already closed */
	if (!priv)
		return TRUE;

	if (!REMMINA_IS_CONNECTION_WINDOW(cnnwin))
		return TRUE;

	nopen = rcw_count_open_connections(cnnwin);

	if (priv->is_main && rcw_app_exiting) {
		/* The application quits right after this: just close the connections */
		rcw_close_all_connections(cnnwin);
		return TRUE;
	}

	if (priv->is_main && !rcw_app_quitting && g_list_length(rcw_windows) > 1) {
		/* Other windows stay open: this one closes alone, with its tabs */
		if (priv->closing_window)
			return TRUE;
		priv->closing_window = TRUE;
		/* Its session, as it is now, goes among the recently closed */
		rcw_session_freeze(cnnwin);
		if (priv->session)
			remmina_session_close(priv->session->id);
		rcw_sessions_refresh_all();
		if (cnnwin == rcw_main) {
			GList *l;
			for (l = rcw_windows; l; l = l->next)
				if (l->data != cnnwin) {
					rcw_set_current(l->data);
					break;
				}
		}
		gtk_widget_hide(GTK_WIDGET(cnnwin));
		if (!rcw_has_connection_pages(cnnwin)) {
			rcw_close_all_connections(cnnwin);	/* tool tabs */
			gtk_widget_destroy(GTK_WIDGET(cnnwin));
			return TRUE;
		}
		/* rcw_on_page_removed() destroys the window when its last tab is gone, or the watchdog */
		if (!priv->quit_watchdog_eventsource)
			priv->quit_watchdog_eventsource = g_timeout_add(5000, rcw_close_window_watchdog, cnnwin);
		rcw_close_all_connections(cnnwin);
		return TRUE;
	}

	if (priv->is_main) {
		/* Closing the last application window means quitting Unidesk */
		if (priv->quit_requested)
			return TRUE;
		rcw_app_quitting = TRUE;
		if (nopen > 0 && remmina_pref.confirm_close_window && priv->on_delete_confirm_mode != RCW_ONDELETE_NOCONFIRM) {
			dialog = gtk_message_dialog_new(GTK_WINDOW(cnnwin), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
							GTK_BUTTONS_YES_NO,
							ngettext("Quit UniDeskBrowser and close %i active connection?",
								 "Quit UniDeskBrowser and close %i active connections?", nopen), nopen);
			i = gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			if (i != GTK_RESPONSE_YES) {
				rcw_app_quitting = FALSE;
				return FALSE;
			}
		}
		priv->quit_requested = TRUE;
		/* The sessions stay "open": "Continue" brings them back at the next start */
		rcw_session_freeze_all();
		if (!rcw_has_connection_pages(cnnwin)) {
			g_idle_add(rcw_quit_idle, NULL);
			return TRUE;
		}
		/* Tabs are closed asynchronously (plugins emit "disconnect" later); the application
		 * quits from rcw_on_page_removed() when the last tab is gone, or from the watchdog */
		if (!priv->quit_watchdog_eventsource)
			priv->quit_watchdog_eventsource = g_timeout_add(5000, rcw_quit_watchdog, cnnwin);
		rcw_close_all_connections(cnnwin);
		return TRUE;
	}

	/* A fullscreen window: closing it (Alt+F4) brings the tabs back into their application window */
	if (rcw_main && priv->on_delete_confirm_mode != RCW_ONDELETE_NOCONFIRM) {
		rcw_switch_viewmode(cnnwin, SCROLLED_WINDOW_MODE);
		return TRUE;
	}

	rcw_close_all_connections(cnnwin);

	return TRUE;
}

static gboolean rcw_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);
	rcw_delete(RCW(widget));
	return TRUE;
}

/* A tab: a connection, or a restored one not connected yet (not the home tab, not a tool tab) */
static gboolean rcw_page_is_tab(GtkWidget *page)
{
	return page && (!rcw_page_is_tool(page) || g_object_get_data(G_OBJECT(page), "session-tab"));
}

static guint rcw_count_tabs(RemminaConnectionWindow *cnnwin)
{
	GtkNotebook *notebook;
	guint n = 0;
	gint i;

	if (!cnnwin || !cnnwin->priv || !cnnwin->priv->notebook || !GTK_IS_NOTEBOOK(cnnwin->priv->notebook))
		return 0;
	notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	for (i = 0; i < gtk_notebook_get_n_pages(notebook); i++)
		if (rcw_page_is_tab(gtk_notebook_get_nth_page(notebook, i)))
			n++;
	return n;
}

/* As in browsers: a window whose last tab left (closed, or moved to another window) closes. Not the
 * last window: closing a tab does not quit the application, the window keeps its home tab. */
static gboolean rcw_close_if_empty_idle(gpointer data)
{
	RemminaConnectionWindow *cnnwin = data;
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->empty_check_id = 0;
	if (priv->is_main && !priv->closing_window && !priv->fullscreen_win && !rcw_app_quitting && !rcw_app_exiting &&
	    g_list_length(rcw_windows) > 1 && rcw_count_tabs(cnnwin) == 0)
		rcw_delete(cnnwin);
	return G_SOURCE_REMOVE;
}

/* Later: a tab leaving may be on its way somewhere (a placeholder replaced, a fullscreen window) */
static void rcw_close_if_empty_later(RemminaConnectionWindow *cnnwin)
{
	if (cnnwin && cnnwin->priv && cnnwin->priv->is_main && !cnnwin->priv->empty_check_id)
		cnnwin->priv->empty_check_id = g_idle_add(rcw_close_if_empty_idle, cnnwin);
}

static void rcw_destroy(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return;

	cnnwin = (RemminaConnectionWindow *)widget;
	priv = cnnwin->priv;

	if (priv->is_main) {
		rcw_windows = g_list_remove(rcw_windows, cnnwin);
		if (priv->session_save_id) {
			g_source_remove(priv->session_save_id);
			priv->session_save_id = 0;
		}
		g_clear_pointer(&priv->session, remmina_session_free);
		if (priv->fullscreen_win && priv->fullscreen_win->priv)
			priv->fullscreen_win->priv->origin_win = NULL;
		if (cnnwin == rcw_main) {
			/* Another window takes over (and the connection list, unless the application is ending) */
			rcw_main = NULL;
			if (rcw_windows && !rcw_app_quitting)
				rcw_set_current(rcw_windows->data);
		}
	} else if (priv->origin_win && priv->origin_win->priv && priv->origin_win->priv->fullscreen_win == cnnwin) {
		priv->origin_win->priv->fullscreen_win = NULL;
	}

	rcw_kp_ungrab(cnnwin);

	if (priv->quit_watchdog_eventsource) {
		g_source_remove(priv->quit_watchdog_eventsource);
		priv->quit_watchdog_eventsource = 0;
	}
	if (priv->acs_eventsourceid) {
		g_source_remove(priv->acs_eventsourceid);
		priv->acs_eventsourceid = 0;
	}
	if (priv->spf_eventsourceid) {
		g_source_remove(priv->spf_eventsourceid);
		priv->spf_eventsourceid = 0;
	}
	if (priv->grab_retry_eventsourceid) {
		g_source_remove(priv->grab_retry_eventsourceid);
		priv->grab_retry_eventsourceid = 0;
	}
	if (priv->grab_realize_source_id) {
		g_source_remove(priv->grab_realize_source_id);
		priv->grab_realize_source_id = 0;
	}
	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}
	if (priv->ftb_hide_eventsource) {
		g_source_remove(priv->ftb_hide_eventsource);
		priv->ftb_hide_eventsource = 0;
	}
	if (priv->ftb_show_eventsource) {
		g_source_remove(priv->ftb_show_eventsource);
		priv->ftb_show_eventsource = 0;
	}
	if (priv->tar_eventsource) {
		g_source_remove(priv->tar_eventsource);
		priv->tar_eventsource = 0;
	}
	if (priv->hidetb_eventsource) {
		g_source_remove(priv->hidetb_eventsource);
		priv->hidetb_eventsource = 0;
	}
	if (priv->dwp_eventsourceid) {
		g_source_remove(priv->dwp_eventsourceid);
		priv->dwp_eventsourceid = 0;
	}
	if (priv->empty_check_id) {
		g_source_remove(priv->empty_check_id);
		priv->empty_check_id = 0;
	}
	rcw_dispose_widget_areas(priv);

	/* There is no need to destroy priv->floating_toolbar_widget,
	 * because it’s our child and will be destroyed automatically */

	cnnwin->priv = NULL;
	g_free(priv);
}

static gboolean rcw_notify_widget_toolbar_placement(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	GType rcwtype;

	rcwtype = rcw_get_type();
	if (G_TYPE_CHECK_INSTANCE_TYPE(widget, rcwtype)) {
		g_signal_emit_by_name(G_OBJECT(widget), "toolbar-place");
		return TRUE;
	}
	return FALSE;
}

static gboolean rcw_tb_drag_failed(GtkWidget *widget, GdkDragContext *context,
				   GtkDragResult result, gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;


	cnnwin = (RemminaConnectionWindow *)user_data;
	priv = cnnwin->priv;

	if (priv->toolbar)
		gtk_widget_show(GTK_WIDGET(priv->toolbar));

	return TRUE;
}

static gboolean rcw_tb_drag_drop(GtkWidget *widget, GdkDragContext *context,
				 gint x, gint y, guint time, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkAllocation wa;
	gint new_toolbar_placement;
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;

	cnnwin = (RemminaConnectionWindow *)user_data;
	priv = cnnwin->priv;

	/* Only the toolbar itself moves it: files dropped beside the SFTP panels must not */
	if (gtk_drag_dest_find_target(widget, context, NULL) == GDK_NONE)
		return FALSE;

	gtk_widget_get_allocation(widget, &wa);

	if (wa.width * y >= wa.height * x) {
		if (wa.width * y > wa.height * (wa.width - x))
			new_toolbar_placement = TOOLBAR_PLACEMENT_BOTTOM;
		else
			new_toolbar_placement = TOOLBAR_PLACEMENT_LEFT;
	} else {
		if (wa.width * y > wa.height * (wa.width - x))
			new_toolbar_placement = TOOLBAR_PLACEMENT_RIGHT;
		else
			new_toolbar_placement = TOOLBAR_PLACEMENT_TOP;
	}

	gtk_drag_finish(context, TRUE, TRUE, time);

	if (new_toolbar_placement != remmina_pref.toolbar_placement) {
		/* Save new position */
		remmina_pref.toolbar_placement = new_toolbar_placement;
		remmina_pref_save();

		/* Signal all windows that the toolbar must be moved */
		remmina_widget_pool_foreach(rcw_notify_widget_toolbar_placement, NULL);
	}
	if (priv->toolbar)
		gtk_widget_show(GTK_WIDGET(priv->toolbar));

	return TRUE;
}

static void rcw_tb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	TRACE_CALL(__func__);

	cairo_surface_t *surface;
	cairo_t *cr;
	GtkAllocation wa;
	double dashes[] = { 10 };

	gtk_widget_get_allocation(widget, &wa);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
	cairo_set_line_width(cr, 4);
	cairo_set_dash(cr, dashes, 1, 0);
	cairo_rectangle(cr, 0, 0, 16, 16);
	cairo_stroke(cr);
	cairo_destroy(cr);

	gtk_widget_hide(widget);

	gtk_drag_set_icon_surface(context, surface);
}

static void rcw_update_toolbar_opacity(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;

	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj) return;

	priv->floating_toolbar_opacity = (1.0 - TOOLBAR_OPACITY_MIN) / ((gdouble)TOOLBAR_OPACITY_LEVEL)
					 * ((gdouble)(TOOLBAR_OPACITY_LEVEL - remmina_file_get_int(cnnobj->remmina_file, "toolbar_opacity", 0)))
					 + TOOLBAR_OPACITY_MIN;
	if (priv->floating_toolbar_widget)
		gtk_widget_set_opacity(GTK_WIDGET(priv->overlay_ftb_overlay), priv->floating_toolbar_opacity);
}

static gboolean rcw_floating_toolbar_make_invisible(gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = (RemminaConnectionWindowPriv *)data;

	gtk_widget_set_opacity(GTK_WIDGET(priv->overlay_ftb_overlay), 0.0);
	priv->ftb_hide_eventsource = 0;
	return G_SOURCE_REMOVE;
}

static void rcw_floating_toolbar_show(RemminaConnectionWindow *cnnwin, gboolean show)
{
	TRACE_CALL(__func__);
	if (cnnwin == NULL || cnnwin->priv == NULL)
		return;
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->floating_toolbar_widget == NULL)
		return;

	if (show || priv->pin_down) {
		if (priv->ftb_show_eventsource) {
			g_source_remove(priv->ftb_show_eventsource);
			priv->ftb_show_eventsource = 0;
		}
		/* Make the FTB no longer transparent, in case we have an hidden toolbar */
		rcw_update_toolbar_opacity(cnnwin);
		/* Remove outstanding hide events, if not yet active */
		if (priv->ftb_hide_eventsource) {
			g_source_remove(priv->ftb_hide_eventsource);
			priv->ftb_hide_eventsource = 0;
		}
	} else {
		/* If we are hiding and the toolbar must be made invisible, schedule
		 * a later toolbar hide */
		if (remmina_pref.fullscreen_toolbar_visibility == FLOATING_TOOLBAR_VISIBILITY_INVISIBLE)
			if (priv->ftb_hide_eventsource == 0)
				priv->ftb_hide_eventsource = g_timeout_add(1000, rcw_floating_toolbar_make_invisible, priv);
	}

	gtk_revealer_set_reveal_child(GTK_REVEALER(priv->revealer), show || priv->pin_down);
}

static void rco_get_desktop_size(RemminaConnectionObject *cnnobj, gint *width, gint *height)
{
	TRACE_CALL(__func__);
	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);


	*width = remmina_protocol_widget_get_width(gp);
	*height = remmina_protocol_widget_get_height(gp);
	if (*width == 0) {
		/* Before connecting we do not have real remote width/height,
		 * so we ask profile values */
		*width = remmina_protocol_widget_get_profile_remote_width(gp);
		*height = remmina_protocol_widget_get_profile_remote_height(gp);
	}
}

static void rco_set_scrolled_policy(RemminaScaleMode scalemode, GtkScrolledWindow *scrolled_window)
{
	TRACE_CALL(__func__);

	gtk_scrolled_window_set_policy(scrolled_window,
				       scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC,
				       scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC);
}

static GtkWidget *rco_create_scrolled_container(RemminaScaleMode scalemode, int view_mode)
{
	GtkWidget *scrolled_container;

	if (view_mode == VIEWPORT_FULLSCREEN_MODE) {
		scrolled_container = remmina_scrolled_viewport_new();
	} else {
		scrolled_container = gtk_scrolled_window_new(NULL, NULL);
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(scrolled_container));
		gtk_container_set_border_width(GTK_CONTAINER(scrolled_container), 0);
		gtk_widget_set_can_focus(scrolled_container, FALSE);
	}

	gtk_widget_set_name(scrolled_container, "remmina-scrolled-container");
	gtk_widget_show(scrolled_container);

	return scrolled_container;
}

static gboolean rcw_toolbar_autofit_restore(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;
	gint dwidth, dheight, ww, wh;
	GtkAllocation ca;

	cnnwin->priv->tar_eventsource = 0;

	if (priv->toolbar_is_reconfiguring)
		return G_SOURCE_REMOVE;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	if (cnnobj->connected && GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		rco_get_desktop_size(cnnobj, &dwidth, &dheight);
		gtk_window_get_size(GTK_WINDOW(cnnobj->cnnwin), &ww, &wh);
		gtk_widget_get_allocation(cnnobj->scrolled_container, &ca);
		/* Grow or shrink the window by the difference between the remote desktop and the
		 * area currently given to the connection: this accounts for the toolbar in any
		 * position, the tab bar, the sidebar and the header bar */
		gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), MAX(1, dwidth + (ww - ca.width)),
				  MAX(1, dheight + (wh - ca.height)));
		gtk_container_check_resize(GTK_CONTAINER(cnnobj->cnnwin));
	}
	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		RemminaScaleMode scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
	}

	return G_SOURCE_REMOVE;
}

static void rcw_toolbar_autofit(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		if ((gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(cnnwin))) & GDK_WINDOW_STATE_MAXIMIZED) != 0)
			gtk_window_unmaximize(GTK_WINDOW(cnnwin));

		/* It’s tricky to make the toolbars disappear automatically, while keeping scrollable.
		 * Please tell me if you know a better way to do this */
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), GTK_POLICY_NEVER, GTK_POLICY_NEVER);

		cnnwin->priv->tar_eventsource = g_timeout_add(200, (GSourceFunc)rcw_toolbar_autofit_restore, cnnwin);
	}
}

void rco_get_monitor_geometry(RemminaConnectionObject *cnnobj, GdkRectangle *sz)
{
	TRACE_CALL(__func__);

	/* Fill sz with the monitor (or workarea) size and position
	 * of the monitor (or workarea) where cnnobj->cnnwin is located */

	GdkRectangle monitor_geometry;

	sz->x = sz->y = sz->width = sz->height = 0;

	if (!cnnobj)
		return;
	if (!cnnobj->cnnwin)
		return;
	if (!gtk_widget_is_visible(GTK_WIDGET(cnnobj->cnnwin)))
		return;

#if GTK_CHECK_VERSION(3, 22, 0)
	GdkDisplay *display;
	GdkMonitor *monitor;
	display = gtk_widget_get_display(GTK_WIDGET(cnnobj->cnnwin));
	monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(GTK_WIDGET(cnnobj->cnnwin)));
#else
	GdkScreen *screen;
	gint monitor;
	screen = gtk_window_get_screen(GTK_WINDOW(cnnobj->cnnwin));
	monitor = gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(GTK_WIDGET(cnnobj->cnnwin)));
#endif

#if GTK_CHECK_VERSION(3, 22, 0)
	gdk_monitor_get_workarea(monitor, &monitor_geometry);
	/* Under Wayland, GTK 3.22, all values returned by gdk_monitor_get_geometry()
	 * and gdk_monitor_get_workarea() seem to have been divided by the
	 * gdk scale factor, so we need to adjust the returned rect
	 * undoing the division */
#ifdef GDK_WINDOWING_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY(display)) {
		int monitor_scale_factor = gdk_monitor_get_scale_factor(monitor);
		monitor_geometry.width *= monitor_scale_factor;
		monitor_geometry.height *= monitor_scale_factor;
	}
#endif
#elif gdk_screen_get_monitor_workarea
	gdk_screen_get_monitor_workarea(screen, monitor, &monitor_geometry);
#else
	gdk_screen_get_monitor_geometry(screen, monitor, &monitor_geometry);
#endif
	*sz = monitor_geometry;
}

static void rco_check_resize(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	gboolean scroll_required = FALSE;

	GdkRectangle monitor_geometry;
	gint rd_width, rd_height;
	gint bordersz;
	gint scalemode;

	scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	/* Get remote destkop size */
	rco_get_desktop_size(cnnobj, &rd_width, &rd_height);

	/* Get our monitor size */
	rco_get_monitor_geometry(cnnobj, &monitor_geometry);

	if (!remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)) &&
	    (monitor_geometry.width < rd_width || monitor_geometry.height < rd_height) &&
	    scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE)
		scroll_required = TRUE;

	switch (cnnobj->cnnwin->priv->view_mode) {
	case SCROLLED_FULLSCREEN_MODE:
		gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), monitor_geometry.width, monitor_geometry.height);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container),
					       (scroll_required ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER),
					       (scroll_required ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER));
		break;

	case VIEWPORT_FULLSCREEN_MODE:
		bordersz = scroll_required ? SCROLL_BORDER_SIZE : 0;
		gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), monitor_geometry.width, monitor_geometry.height);
		if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container))
			/* Put a border around Notebook content (RemminaScrolledViewpord), so we can
			 * move the mouse over the border to scroll */
			gtk_container_set_border_width(GTK_CONTAINER(cnnobj->scrolled_container), bordersz);

		break;

	case SCROLLED_WINDOW_MODE:
		/* Unidesk: the application window keeps its own size, connections adapt to it */
		break;

	default:
		break;
	}
}

static void rcw_set_tooltip(GtkWidget *item, const gchar *tip, guint key1, guint key2)
{
	TRACE_CALL(__func__);
	gchar *s1;
	gchar *s2;

	if (remmina_pref.hostkey && key1) {
		if (key2)
			s1 = g_strdup_printf(" (%s + %s,%s)", gdk_keyval_name(remmina_pref.hostkey),
					     gdk_keyval_name(gdk_keyval_to_upper(key1)), gdk_keyval_name(gdk_keyval_to_upper(key2)));
		else if (key1 == remmina_pref.hostkey)
			s1 = g_strdup_printf(" (%s)", gdk_keyval_name(remmina_pref.hostkey));
		else
			s1 = g_strdup_printf(" (%s + %s)", gdk_keyval_name(remmina_pref.hostkey),
					     gdk_keyval_name(gdk_keyval_to_upper(key1)));
	} else {
		s1 = NULL;
	}
	s2 = g_strdup_printf("%s%s", tip, s1 ? s1 : "");
	gtk_widget_set_tooltip_text(item, s2);
	g_free(s2);
	g_free(s1);
}

static void remmina_protocol_widget_update_alignment(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaScaleMode scalemode;
	gboolean scaledexpandedmode;
	int rdwidth, rdheight;
	gfloat aratio;

	if (!cnnobj->plugin_can_scale) {
		/* If we have a plugin that cannot scale,
		 * (i.e. SFTP plugin), then we expand proto */
		gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
	} else {
		/* Plugin can scale */

		scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
		scaledexpandedmode = remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

		/* Check if we need aspectframe and create/destroy it accordingly */
		if (scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED && !scaledexpandedmode) {
			/* We need an aspectframe as a parent of proto */
			rdwidth = remmina_protocol_widget_get_width(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
			rdheight = remmina_protocol_widget_get_height(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
			aratio = (gfloat)rdwidth / (gfloat)rdheight;
			if (!cnnobj->aspectframe) {
				/* We need a new aspectframe */
				cnnobj->aspectframe = gtk_aspect_frame_new(NULL, 0.5, 0.5, aratio, FALSE);
				gtk_widget_set_name(cnnobj->aspectframe, "remmina-cw-aspectframe");
				gtk_frame_set_shadow_type(GTK_FRAME(cnnobj->aspectframe), GTK_SHADOW_NONE);
				g_object_ref(cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);
				gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->aspectframe);
				gtk_container_add(GTK_CONTAINER(cnnobj->aspectframe), cnnobj->proto);
				g_object_unref(cnnobj->proto);
				gtk_widget_show(cnnobj->aspectframe);
				if (cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv->notebook != NULL)
					rcw_grab_focus(cnnobj->cnnwin);
			} else {
				gtk_aspect_frame_set(GTK_ASPECT_FRAME(cnnobj->aspectframe), 0.5, 0.5, aratio, FALSE);
			}
		} else {
			/* We do not need an aspectframe as a parent of proto */
			if (cnnobj->aspectframe) {
				/* We must remove the old aspectframe reparenting proto to viewport */
				g_object_ref(cnnobj->aspectframe);
				g_object_ref(cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->aspectframe), cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->viewport), cnnobj->aspectframe);
				g_object_unref(cnnobj->aspectframe);
				cnnobj->aspectframe = NULL;
				gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);
				g_object_unref(cnnobj->proto);
				if (cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv->notebook != NULL)
					rcw_grab_focus(cnnobj->cnnwin);
			}
		}

		if (scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED || scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES) {
			/* We have a plugin that can be scaled, and the scale button
			 * has been pressed. Give it the correct WxH maintaining aspect
			 * ratio of remote destkop size */
			gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
			gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
		} else {
			/* Plugin can scale, but no scaling is active. Ensure that we have
			 * aspectframe with a ratio of 1 */
			gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_CENTER);
			gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_CENTER);
		}
	}
}

static void nb_set_current_page(GtkNotebook *notebook, GtkWidget *page)
{
	gint np, i;

	np = gtk_notebook_get_n_pages(notebook);
	for (i = 0; i < np; i++) {
		if (gtk_notebook_get_nth_page(notebook, i) == page) {
			gtk_notebook_set_current_page(notebook, i);
			break;
		}
	}
}

static void nb_migrate_message_panels(GtkWidget *frompage, GtkWidget *topage)
{
	/* Migrate a single connection tab from a notebook to another one */
	GList *lst, *l;

	/* Reparent message panels */
	lst = gtk_container_get_children(GTK_CONTAINER(frompage));
	for (l = lst; l != NULL; l = l->next) {
		if (REMMINA_IS_MESSAGE_PANEL(l->data)) {
			g_object_ref(l->data);
			gtk_container_remove(GTK_CONTAINER(frompage), GTK_WIDGET(l->data));
			gtk_container_add(GTK_CONTAINER(topage), GTK_WIDGET(l->data));
			g_object_unref(l->data);
			gtk_box_reorder_child(GTK_BOX(topage), GTK_WIDGET(l->data), 0);
		}
	}
	g_list_free(lst);

}

static GtkWidget *rcw_append_session_tab(RemminaConnectionWindow *cnnwin, const RemminaSessionTab *tab);

/* Move one connection tab to another window; the new page (NULL if it could not move). A restored
 * tab not connected yet moves too; tool tabs stay. */
static GtkWidget *rcw_migrate_page(RemminaConnectionWindow *from, RemminaConnectionWindow *to, GtkWidget *frompage)
{
	GtkNotebook *from_notebook = from->priv->notebook, *to_notebook = to->priv->notebook;
	RemminaConnectionObject *cnnobj = g_object_get_data(G_OBJECT(frompage), "cnnobj");
	GList *list = g_object_get_data(G_OBJECT(frompage), "cnnobj-list"), *l;
	GtkWidget *newpage, *old_scrolled_container, *label;
	RemminaSessionTab *waiting = g_object_get_data(G_OBJECT(frompage), "session-tab");
	RemminaScaleMode scalemode;

	if (waiting) {
		newpage = rcw_append_session_tab(to, waiting);
		gtk_notebook_remove_page(from_notebook, gtk_notebook_page_num(from_notebook, frompage));
		return newpage;
	}

	if (list && g_list_length(list) > 1 && cnnobj) {
		/* A split page (several terminal panes): move the whole page as it is */
		g_object_ref(frompage);
		label = rco_create_tab_label(cnnobj);
		gtk_notebook_remove_page(from_notebook, gtk_notebook_page_num(from_notebook, frompage));
		gtk_notebook_append_page(to_notebook, frompage, label);
		gtk_notebook_set_tab_reorderable(to_notebook, frompage, TRUE);
		gtk_widget_set_can_focus(gtk_widget_get_parent(label), FALSE);
		for (l = list; l; l = l->next) {
			RemminaConnectionObject *co = (RemminaConnectionObject *)l->data;
			if (co) {
				co->cnnwin = to;
				co->tab_page = frompage;
			}
		}
		g_object_unref(frompage);
		return frompage;
	}

	if (!cnnobj) {
		if (!rcw_page_is_tool(frompage))
			gtk_notebook_remove_page(from_notebook, gtk_notebook_page_num(from_notebook, frompage));
		return NULL;
	}

	/* A scrolled container must be recreated, because it can be different on the new window/page
	 * depending on view_mode */
	scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
	old_scrolled_container = cnnobj->scrolled_container;
	cnnobj->scrolled_container = rco_create_scrolled_container(scalemode, to->priv->view_mode);

	newpage = rcw_append_new_page(to, cnnobj);

	nb_migrate_message_panels(frompage, newpage);

	/* Reparent the viewport (which is inside scrolled_container) to the new page */
	g_object_ref(cnnobj->viewport);
	gtk_container_remove(GTK_CONTAINER(old_scrolled_container), cnnobj->viewport);
	gtk_container_add(GTK_CONTAINER(cnnobj->scrolled_container), cnnobj->viewport);
	g_object_unref(cnnobj->viewport);

	gtk_widget_destroy(old_scrolled_container);

	/* Remove the source page (a fullscreen window destroys itself when it has no tabs left) */
	gtk_notebook_remove_page(from_notebook, gtk_notebook_page_num(from_notebook, frompage));
	return newpage;
}

static void rcw_migrate(RemminaConnectionWindow *from, RemminaConnectionWindow *to)
{
	/* Migrate the connection tabs of a window to another one. Tool tabs (profile
	 * editor, preferences…) never leave the application window. */

	gint cp, np, i, newcur;
	GtkNotebook *from_notebook, *to_notebook;
	GtkWidget *frompage;
	GPtrArray *pages;

	from_notebook = from->priv->notebook;
	to_notebook = to->priv->notebook;
	if (!from_notebook || !GTK_IS_NOTEBOOK(from_notebook) || !to_notebook)
		return;

	cp = gtk_notebook_get_current_page(from_notebook);
	np = gtk_notebook_get_n_pages(from_notebook);
	newcur = -1;

	/* Collect the pages first: removing pages while iterating shifts the indexes */
	pages = g_ptr_array_new();
	for (i = 0; i < np; i++) {
		frompage = gtk_notebook_get_nth_page(from_notebook, i);
		/* Restored tabs not connected yet go along */
		if (rcw_page_is_tool(frompage) && !g_object_get_data(G_OBJECT(frompage), "session-tab"))
			continue;
		if (i == cp)
			newcur = gtk_notebook_get_n_pages(to_notebook) + pages->len;
		g_ptr_array_add(pages, frompage);
	}

	for (i = 0; i < pages->len; i++)
		rcw_migrate_page(from, to, (GtkWidget *)g_ptr_array_index(pages, i));

	if (newcur >= 0 && newcur < gtk_notebook_get_n_pages(to_notebook))
		gtk_notebook_set_current_page(to_notebook, newcur);
	else if (pages->len > 0)
		gtk_notebook_set_current_page(to_notebook, gtk_notebook_get_n_pages(to_notebook) - 1);
	g_ptr_array_free(pages, TRUE);
}

/* Move the tabs of cnnwin into a new fullscreen window; the application window hides meanwhile */
static RemminaConnectionWindow *rcw_enter_fullscreen_window(RemminaConnectionWindow *cnnwin, gint view_mode)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *newwin;
	gboolean from_main = (cnnwin->priv && cnnwin->priv->is_main);
	/* The application window the tabs come back to */
	RemminaConnectionWindow *origin = from_main ? cnnwin : cnnwin->priv->origin_win;

	newwin = rcw_create_fullscreen(GTK_WINDOW(cnnwin), view_mode);
	newwin->priv->origin_win = origin;
	if (origin && origin->priv)
		origin->priv->fullscreen_win = newwin;
	rcw_migrate(cnnwin, newwin);
	if (from_main)
		gtk_widget_hide(GTK_WIDGET(cnnwin));
	/* Prevent unreleased hostkey from old window to be released here */
	newwin->priv->hostkey_used = TRUE;
	return newwin;
}

static void rcw_switch_viewmode(RemminaConnectionWindow *cnnwin, int newmode)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *main;
	int old_mode;

	old_mode = cnnwin->priv->view_mode;
	if (old_mode == newmode)
		return;

	if (newmode == VIEWPORT_FULLSCREEN_MODE || newmode == SCROLLED_FULLSCREEN_MODE) {
		rcw_enter_fullscreen_window(cnnwin, cnnwin->priv->fss_view_mode);
	} else {
		/* Back into the application window (it is shown again by rcw_on_page_removed
		 * when the fullscreen window empties, and explicitly below) */
		main = cnnwin->priv->origin_win && cnnwin->priv->origin_win->priv ? cnnwin->priv->origin_win : rcw_get_main();
		if (old_mode == VIEWPORT_FULLSCREEN_MODE || old_mode == SCROLLED_FULLSCREEN_MODE)
			main->priv->fss_view_mode = old_mode;
		rcw_migrate(cnnwin, main);
		if (main->priv->fullscreen_win == cnnwin)
			main->priv->fullscreen_win = NULL;
		main->priv->hostkey_used = TRUE;
		main->priv->hostkey_activated = FALSE;
		main->priv->toolbar_is_reconfiguring = TRUE;
		if (main->priv->toolitem_fullscreen)
			gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(main->priv->toolitem_fullscreen), FALSE);
		if (main->priv->fullscreen_option_button)
			gtk_widget_set_sensitive(main->priv->fullscreen_option_button, FALSE);
		main->priv->toolbar_is_reconfiguring = FALSE;
		rcw_dispose_widget_areas(main->priv);
		gtk_widget_show(GTK_WIDGET(main));
		gtk_window_present(GTK_WINDOW(main));
	}
}


static void rcw_toolbar_fullscreen(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	if (remmina_protocol_widget_get_multimon(gp) >= 1) {
		REMMINA_DEBUG("Fullscreen on all monitor");
	} else {
		REMMINA_DEBUG("Fullscreen on one monitor");
	}

	if ((toggle != NULL && toggle == cnnwin->priv->toolitem_fullscreen)) {
		if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle))) {
			if (remmina_protocol_widget_get_multimon(gp) >= 1) {
				gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_multimon), TRUE);
				cnnwin->priv->multi_mon = TRUE;
			}
			rcw_switch_viewmode(cnnwin, cnnwin->priv->fss_view_mode);
		} else {
			rcw_switch_viewmode(cnnwin, SCROLLED_WINDOW_MODE);
			remmina_file_set_int(cnnobj->remmina_file, "multimon", 0);
			remmina_file_save(cnnobj->remmina_file);
		}
	} else if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_multimon))) {
		rcw_switch_viewmode(cnnwin, cnnwin->priv->fss_view_mode);
	} else {
		rcw_switch_viewmode(cnnwin, SCROLLED_WINDOW_MODE);
	}
}

static void rco_viewport_fullscreen_mode(GtkWidget *widget, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *newwin;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj->cnnwin->priv->fss_view_mode = VIEWPORT_FULLSCREEN_MODE;
	newwin = rcw_enter_fullscreen_window(cnnobj->cnnwin, VIEWPORT_FULLSCREEN_MODE);
	(void)newwin;
	rcw_create_widget_areas(cnnobj);
}

static void rco_scrolled_fullscreen_mode(GtkWidget *widget, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *newwin;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj->cnnwin->priv->fss_view_mode = SCROLLED_FULLSCREEN_MODE;
	newwin = rcw_enter_fullscreen_window(cnnobj->cnnwin, SCROLLED_FULLSCREEN_MODE);
	(void)newwin;
}

static void rcw_fullscreen_option_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->sticky = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->fullscreen_option_button), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_fullscreen_option(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem;
	GSList *group;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		return;

	cnnwin->priv->sticky = TRUE;

	menu = gtk_menu_new();

	menuitem = gtk_radio_menu_item_new_with_label(NULL, _("Viewport fullscreen mode"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
	if (cnnwin->priv->view_mode == VIEWPORT_FULLSCREEN_MODE)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rco_viewport_fullscreen_mode), cnnobj);

	menuitem = gtk_radio_menu_item_new_with_label(group, _("Scrolled fullscreen"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	if (cnnwin->priv->view_mode == SCROLLED_FULLSCREEN_MODE)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rco_scrolled_fullscreen_mode), cnnobj);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_fullscreen_option_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, cnnwin->priv->toolitem_fullscreen, 0,
		       gtk_get_current_event_time());
#endif
}


static void rcw_scaler_option_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;
	priv->sticky = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->scaler_option_button), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_scaler_expand(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return;
	remmina_protocol_widget_set_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), TRUE);
	remmina_file_set_int(cnnobj->remmina_file, "scaler_expand", TRUE);
	remmina_protocol_widget_update_alignment(cnnobj);
}
static void rcw_scaler_keep_aspect(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return;

	remmina_protocol_widget_set_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), FALSE);
	remmina_file_set_int(cnnobj->remmina_file, "scaler_expand", FALSE);
	remmina_protocol_widget_update_alignment(cnnobj);
}

static void rcw_toolbar_scaler_option(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem;
	GSList *group;
	gboolean scaler_expand;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnwin->priv;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		return;

	scaler_expand = remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	priv->sticky = TRUE;

	menu = gtk_menu_new();

	menuitem = gtk_radio_menu_item_new_with_label(NULL, _("Keep aspect ratio when scaled"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
	if (!scaler_expand)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rcw_scaler_keep_aspect), cnnwin);

	menuitem = gtk_radio_menu_item_new_with_label(group, _("Fill client window when scaled"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	if (scaler_expand)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rcw_scaler_expand), cnnwin);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_scaler_option_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, priv->toolitem_scale, 0,
		       gtk_get_current_event_time());
#endif
}

static void rcw_switch_page_activate(GtkMenuItem *menuitem, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	gint page_num;

	page_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem), "new-page-num"));
	gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), page_num);
}

static void rcw_toolbar_switch_page_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_switch_page), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_switch_page(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;

	GtkWidget *menu;
	GtkWidget *menuitem;
	GtkWidget *image;
	gint i, n;

	if (priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	menu = gtk_menu_new();

	n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook));
	for (i = 0; i < n; i++) {
		GtkWidget *pg = gtk_notebook_get_nth_page(GTK_NOTEBOOK(priv->notebook), i);
		RemminaConnectionObject *co = rcw_get_cnnobj_at_page(cnnwin, i);

		if (co && co->remmina_file)
			menuitem = gtk_menu_item_new_with_label(remmina_file_get_string(co->remmina_file, "name"));
		else
			menuitem = gtk_menu_item_new_with_label(rcw_tool_page_title(pg));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		if (co && co->remmina_file) {
			image = gtk_image_new_from_icon_name(remmina_file_get_icon_name(co->remmina_file), GTK_ICON_SIZE_MENU);
			gtk_widget_show(image);
		}

		g_object_set_data(G_OBJECT(menuitem), "new-page-num", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(rcw_switch_page_activate), cnnwin);
		if (i == gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)))
			gtk_widget_set_sensitive(menuitem, FALSE);
	}

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_switch_page_popdown),
			 cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rco_update_toolbar_autofit_button(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkToolItem *toolitem;
	RemminaScaleMode sc;

	toolitem = priv->toolitem_autofit;
	if (toolitem) {
		if (priv->view_mode != SCROLLED_WINDOW_MODE) {
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		} else {
			sc = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), sc == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE);
		}
	}
}

static void rco_change_scalemode(RemminaConnectionObject *cnnobj, gboolean bdyn, gboolean bscale)
{
	RemminaScaleMode scalemode;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;

	if (bdyn)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES;
	else if (bscale)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED;
	else
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE;

	remmina_protocol_widget_set_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), scalemode);
	remmina_file_set_int(cnnobj->remmina_file, "scale", scalemode);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED);
	rco_update_toolbar_autofit_button(cnnobj);

	remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
						     REMMINA_PROTOCOL_FEATURE_TYPE_SCALE, 0);

	if (cnnobj->cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		rco_check_resize(cnnobj);
	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
	}
}

static void rcw_toolbar_dynres(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean bdyn, bscale, plugin_has_dynres, plugin_can_scale;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (cnnobj->connected) {
		bdyn = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
		bscale = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_scale));

		plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

		plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

		if (!(plugin_has_dynres && cnnobj->dynres_unlocked))
			bdyn = FALSE;

		if (!plugin_can_scale)
			bscale = FALSE;

		if (bdyn && bscale)
			bscale = FALSE;

		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toggle), bdyn);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_scale), bscale);

		rco_change_scalemode(cnnobj, bdyn, bscale);
	}
}

static void rcw_toolbar_scaled_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean bdyn, bscale, plugin_has_dynres, plugin_can_scale;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (cnnobj->connected) {
		bdyn = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_dynres));
		bscale = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));

		plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

		plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

		if ((!plugin_has_dynres || !cnnobj->dynres_unlocked))
			bdyn = FALSE;

		if (!plugin_can_scale)
			bscale = FALSE;

		if (bdyn && bscale)
			bdyn = FALSE;

		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_dynres), bdyn);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toggle), bscale);

		rco_change_scalemode(cnnobj, bdyn, bscale);
	}
}

static void rcw_toolbar_viewonly_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	// Add viewonly mode to rcw toolbar
	// This will only be available to plugins that support viewonly
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	gboolean bactive = FALSE;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) {
		return;
	}

	// When opening connection, take viewonly value from remmina file
	if (cnnobj->connected) {
		bactive = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_viewonly), bactive);
	}

	// Update remmina file and let the plugin handle the viewonly
	remmina_file_set_int(cnnobj->remmina_file, "viewonly", bactive);
}

static void rcw_toolbar_multi_monitor_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle))) {
		REMMINA_DEBUG("Saving multimon as 1");
		remmina_file_set_int(cnnobj->remmina_file, "multimon", 1);
		remmina_file_save(cnnobj->remmina_file);
		cnnwin->priv->multi_mon = TRUE;
		if (cnnwin->priv->view_mode == VIEWPORT_FULLSCREEN_MODE || cnnwin->priv->view_mode == SCROLLED_FULLSCREEN_MODE) {
			RemminaConnectionWindow* newwin = rcw_create_fullscreen(NULL, cnnwin->priv->view_mode);
			RemminaConnectionWindow *origin = cnnwin->priv->origin_win;
			newwin->priv->origin_win = origin;
			if (origin && origin->priv)
				origin->priv->fullscreen_win = newwin;
			rcw_migrate(cnnwin, newwin);
			cnnwin = newwin;
		}
 		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_fullscreen), TRUE);
		
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
				REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON, 0);
		rcw_create_widget_areas(cnnobj);
	} else {
		REMMINA_DEBUG("Saving multimon as 0");
		remmina_file_set_int(cnnobj->remmina_file, "multimon", 0);
		remmina_file_save(cnnobj->remmina_file);
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
				REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON, 0);
		rcw_toolbar_fullscreen(NULL, cnnwin);
	}
}

static void rcw_toolbar_preferences_popdown(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	cnnobj->cnnwin->priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_preferences), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_creds_popdown(GtkMenuShell *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_creds), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_tools_popdown(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_tools), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rco_call_protocol_feature_radio(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;
	gpointer value;

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
		feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
		value = g_object_get_data(G_OBJECT(menuitem), "feature-value");

		remmina_file_set_string(cnnobj->remmina_file, (const gchar *)feature->opt2, (const gchar *)value);
		remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
	}
}

static void rco_call_protocol_feature_check(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;
	gboolean value;

	feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
	value = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem));
	remmina_file_set_int(cnnobj->remmina_file, (const gchar *)feature->opt2, value);
	remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
}

static void rco_call_protocol_feature_activate(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;

	feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
	remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
}

static void rcw_toolbar_preferences_radio(RemminaConnectionObject *cnnobj, RemminaFile *remminafile,
				   GtkWidget *menu, const RemminaProtocolFeature *feature, const gchar *domain, gboolean enabled)
{
	TRACE_CALL(__func__);
	GtkWidget *menuitem;
	GSList *group;
	gint i;
	const gchar **list;
	const gchar *value;

	group = NULL;
	value = remmina_file_get_string(remminafile, (const gchar *)feature->opt2);
	list = (const gchar **)feature->opt3;
	for (i = 0; list[i]; i += 2) {
		menuitem = gtk_radio_menu_item_new_with_label(group, g_dgettext(domain, list[i + 1]));
		group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		if (enabled) {
			g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);
			g_object_set_data(G_OBJECT(menuitem), "feature-value", (gpointer)list[i]);

			if (value && g_strcmp0(list[i], value) == 0)
				gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);

			g_signal_connect(G_OBJECT(menuitem), "toggled",
					 G_CALLBACK(rco_call_protocol_feature_radio), cnnobj);
		} else {
			gtk_widget_set_sensitive(menuitem, FALSE);
		}
	}
}

static void rcw_toolbar_preferences_check(RemminaConnectionObject *cnnobj,
				   GtkWidget *menu, const RemminaProtocolFeature *feature,
				   const gchar *domain, gboolean enabled)
{
	TRACE_CALL(__func__);
	GtkWidget *menuitem;

	menuitem = gtk_check_menu_item_new_with_label(g_dgettext(domain, (const gchar *)feature->opt3));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

	if (enabled) {
		g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);

		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
					       remmina_file_get_int(cnnobj->remmina_file, (const gchar *)feature->opt2, FALSE));

		g_signal_connect(G_OBJECT(menuitem), "toggled",
				 G_CALLBACK(rco_call_protocol_feature_check), cnnobj);
	} else {
		gtk_widget_set_sensitive(menuitem, FALSE);
	}
}

static void rcw_toolbar_preferences(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	const RemminaProtocolFeature *feature;
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean separator;
	gchar *domain;
	gboolean enabled;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnobj->cnnwin->priv;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	separator = FALSE;

	domain = remmina_protocol_widget_get_domain(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	menu = gtk_menu_new();
	for (feature = remmina_protocol_widget_get_features(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)); feature && feature->type;
	     feature++) {
		if (feature->type != REMMINA_PROTOCOL_FEATURE_TYPE_PREF)
			continue;

		if (separator) {
			menuitem = gtk_separator_menu_item_new();
			gtk_widget_show(menuitem);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			separator = FALSE;
		}
		enabled = remmina_protocol_widget_query_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
		switch (GPOINTER_TO_INT(feature->opt1)) {
		case REMMINA_PROTOCOL_FEATURE_PREF_RADIO:
			rcw_toolbar_preferences_radio(cnnobj, cnnobj->remmina_file, menu, feature,
						      domain, enabled);
			separator = TRUE;
			break;
		case REMMINA_PROTOCOL_FEATURE_PREF_CHECK:
			rcw_toolbar_preferences_check(cnnobj, menu, feature,
						      domain, enabled);
			break;
		}
	}

	g_free(domain);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_preferences_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_creds(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem = NULL;
	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	const gchar* username = remmina_file_get_string(cnnobj->remmina_file, "username");
	const gchar* password = remmina_file_get_string(cnnobj->remmina_file, "password");

	if (username == NULL && password == NULL){
		return;
	}


	menu = gtk_menu_new();
	//username
	if (username != NULL){
		menuitem = gtk_menu_item_new_with_label(_("Insert username"));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		g_object_set_data(G_OBJECT(menuitem), "keystrokes", (gpointer)username);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
										G_CALLBACK(remmina_protocol_widget_send_keystrokes),
										REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}
	
	//password
	if (password != NULL){
			menuitem = gtk_menu_item_new_with_label(_("Insert password"));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		g_object_set_data(G_OBJECT(menuitem), "keystrokes", (gpointer)password);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
										G_CALLBACK(remmina_protocol_widget_send_keystrokes),
										REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}


	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_creds_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_tools(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	const RemminaProtocolFeature *feature;
	GtkWidget *menu;
	GtkWidget *menuitem = NULL;
	GtkMenu *submenu_keystrokes;
	const gchar *domain;
	gboolean enabled;
	gchar **keystrokes;
	gchar **keystroke_values;
	gint i;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnobj->cnnwin->priv;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	domain = remmina_protocol_widget_get_domain(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	menu = gtk_menu_new();
	for (feature = remmina_protocol_widget_get_features(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)); feature && feature->type;
	     feature++) {
		if (feature->type != REMMINA_PROTOCOL_FEATURE_TYPE_TOOL)
			continue;

		if (feature->opt1)
			menuitem = gtk_menu_item_new_with_label(g_dgettext(domain, (const gchar *)feature->opt1));
		if (feature->opt3)
			rcw_set_tooltip(menuitem, "", GPOINTER_TO_UINT(feature->opt3), 0);
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		enabled = remmina_protocol_widget_query_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
		if (enabled) {
			g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);

			g_signal_connect(G_OBJECT(menuitem), "activate",
					 G_CALLBACK(rco_call_protocol_feature_activate), cnnobj);
		} else {
			gtk_widget_set_sensitive(menuitem, FALSE);
		}
	}

	/* If the plugin accepts keystrokes include the keystrokes menu */
	if (remmina_protocol_widget_plugin_receives_keystrokes(REMMINA_PROTOCOL_WIDGET(cnnobj->proto))) {
		/* Get the registered keystrokes list */
		keystrokes = g_strsplit(remmina_pref.keystrokes, STRING_DELIMITOR, -1);

		/* Add a keystrokes submenu */
		menuitem = gtk_menu_item_new_with_label(_("Keystrokes"));
		submenu_keystrokes = GTK_MENU(gtk_menu_new());
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), GTK_WIDGET(submenu_keystrokes));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		/* Add each registered keystroke */
		if (g_strv_length(keystrokes)) {
			for (i = 0; i < g_strv_length(keystrokes); i++) {
				keystroke_values = g_strsplit(keystrokes[i], STRING_DELIMITOR2, -1);
				if (g_strv_length(keystroke_values) > 1) {
					/* Add the keystroke if no description was available */
					menuitem = gtk_menu_item_new_with_label(
						g_strdup(keystroke_values[strlen(keystroke_values[0]) ? 0 : 1]));
					g_object_set_data(G_OBJECT(menuitem), "keystrokes", g_strdup(keystroke_values[1]));
					g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
									G_CALLBACK(remmina_protocol_widget_send_keystrokes),
									REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
					gtk_widget_show(menuitem);
					gtk_menu_shell_append(GTK_MENU_SHELL(submenu_keystrokes), menuitem);
				}
				g_strfreev(keystroke_values);
			}
		}
		menuitem = gtk_menu_item_new_with_label(_("Send clipboard content as keystrokes"));
		static gchar k_tooltip[] =
			N_("CAUTION: Pasted text will be sent as a sequence of key-codes as if typed on your local keyboard.\n"
			"\n"
			"  • For best results use same keyboard settings for both, client and server.\n"
			"\n"
			"  • If client-keyboard is different from server-keyboard the received text can contain wrong or erroneous characters.\n"
			"\n"
			"  • Unicode characters and other special characters that can't be translated to local key-codes won’t be sent to the server.\n"
			"\n");
		gtk_widget_set_tooltip_text(menuitem, k_tooltip);
		gtk_menu_shell_append(GTK_MENU_SHELL(submenu_keystrokes), menuitem);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
						G_CALLBACK(remmina_protocol_widget_send_clipboard),
						REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
		gtk_widget_show(menuitem);

		g_strfreev(keystrokes);
	}

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_tools_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_duplicate(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	remmina_file_save(cnnobj->remmina_file);

	remmina_exec_command(REMMINA_COMMAND_CONNECT, cnnobj->remmina_file->filename);
}

static void rcw_toolbar_edit_pref(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))
		|| cnnobj->remmina_file == NULL
		|| cnnobj->remmina_file->filename == NULL) return;
	GtkWidget* widget = remmina_file_editor_new_from_filename(cnnobj->remmina_file->filename);

	if (widget)
		rcw_main_add_window_as_tab(GTK_WINDOW(widget), NULL, "document-properties-symbolic");
}

static void rcw_toolbar_screenshot(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	GdkPixbuf *screenshot;
	GdkWindow *active_window;
	cairo_t *cr;
	gint width, height;
	GString *pngstr;
	gchar *pngname;
	GtkWidget *dialog;
	RemminaProtocolWidget *gp;
	RemminaPluginScreenshotData rpsd;
	RemminaConnectionObject *cnnobj;
	cairo_surface_t *srcsurface;
	cairo_format_t cairo_format;
	cairo_surface_t *surface;
	int stride;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	GDateTime *date = g_date_time_new_now_utc();

	// We will take a screenshot of the currently displayed RemminaProtocolWidget.
	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	gchar *denyclip = remmina_pref_get_value("deny_screenshot_clipboard");

	REMMINA_DEBUG("deny_screenshot_clipboard is set to %s", denyclip);

	GtkClipboard *c = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	// Ask the plugin if it can give us a screenshot
	if (remmina_protocol_widget_plugin_screenshot(gp, &rpsd)) {
		// Good, we have a screenshot from the plugin !

		REMMINA_DEBUG("Screenshot from plugin: w=%d h=%d bpp=%d bytespp=%d\n",
			      rpsd.width, rpsd.height, rpsd.bitsPerPixel, rpsd.bytesPerPixel);

		width = rpsd.width;
		height = rpsd.height;

		if (rpsd.bitsPerPixel == 32)
			cairo_format = CAIRO_FORMAT_ARGB32;
		else if (rpsd.bitsPerPixel == 24)
			cairo_format = CAIRO_FORMAT_RGB24;
		else
			cairo_format = CAIRO_FORMAT_RGB16_565;

		stride = cairo_format_stride_for_width(cairo_format, width);

		srcsurface = cairo_image_surface_create_for_data(rpsd.buffer, cairo_format, width, height, stride);
		// Transfer the PixBuf in the main clipboard selection
		if (denyclip && (g_strcmp0(denyclip, "true")))
			gtk_clipboard_set_image(c, gdk_pixbuf_get_from_surface(
							srcsurface, 0, 0, width, height));
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
		cr = cairo_create(surface);
		cairo_set_source_surface(cr, srcsurface, 0, 0);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_surface_destroy(srcsurface);

		free(rpsd.buffer);
	} else {
		// The plugin is not releasing us a screenshot, just try to catch one via GTK

		/* Warn the user if image is distorted */
		if (cnnobj->plugin_can_scale &&
		    get_current_allowed_scale_mode(cnnobj, NULL, NULL) == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED) {
			dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
							_("Turn off scaling to avoid screenshot distortion."));
			g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
			gtk_widget_show(dialog);
		}

		// Get the screenshot.
		active_window = gtk_widget_get_window(GTK_WIDGET(gp));
		width = gdk_window_get_width(active_window);
		height = gdk_window_get_height(active_window);

		screenshot = gdk_pixbuf_get_from_window(active_window, 0, 0, width, height);
		if (screenshot == NULL)
			g_print("gdk_pixbuf_get_from_window failed\n");

		// Transfer the PixBuf in the main clipboard selection
		if (denyclip && (g_strcmp0(denyclip, "true")))
			gtk_clipboard_set_image(c, screenshot);
		// Prepare the destination Cairo surface.
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
		cr = cairo_create(surface);

		// Copy the source pixbuf to the surface and paint it.
		gdk_cairo_set_source_pixbuf(cr, screenshot, 0, 0);
		cairo_paint(cr);

		// Deallocate screenshot pixbuf
		g_object_unref(screenshot);
	}

	//home/antenore/Pictures/remmina_%p_%h_%Y  %m %d-%H%M%S.png pngname
	//home/antenore/Pictures/remmina_st_  _2018 9 24-151958.240374.png

	pngstr = g_string_new(g_strdup_printf("%s/%s.png",
					      remmina_pref.screenshot_path,
					      remmina_pref.screenshot_name));
	remmina_utils_string_replace_all(pngstr, "%p",
					 remmina_file_get_string(cnnobj->remmina_file, "name"));
	remmina_utils_string_replace_all(pngstr, "%h",
					 remmina_file_get_string(cnnobj->remmina_file, "server"));
	remmina_utils_string_replace_all(pngstr, "%Y",
					 g_strdup_printf("%d", g_date_time_get_year(date)));
	remmina_utils_string_replace_all(pngstr, "%m", g_strdup_printf("%02d",
								       g_date_time_get_month(date)));
	remmina_utils_string_replace_all(pngstr, "%d",
					 g_strdup_printf("%02d", g_date_time_get_day_of_month(date)));
	remmina_utils_string_replace_all(pngstr, "%H",
					 g_strdup_printf("%02d", g_date_time_get_hour(date)));
	remmina_utils_string_replace_all(pngstr, "%M",
					 g_strdup_printf("%02d", g_date_time_get_minute(date)));
	remmina_utils_string_replace_all(pngstr, "%S",
					 g_strdup_printf("%02d", g_date_time_get_second(date)));
	g_date_time_unref(date);
	pngname = g_string_free(pngstr, FALSE);

	cairo_surface_write_to_png(surface, pngname);

	/* send a desktop notification */
	if (g_file_test(pngname, G_FILE_TEST_EXISTS))
		remmina_public_send_notification("remmina-screenshot-is-ready-id", _("Screenshot taken"), pngname);

	//Clean up and return.
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void rcw_toolbar_minimize(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	rcw_floating_toolbar_show(cnnwin, FALSE);
	gtk_window_iconify(GTK_WINDOW(cnnwin));
	rcw_iconify_widget_areas(cnnwin);
}

static void rcw_toolbar_disconnect(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	rco_disconnect_current_page(cnnobj);
}

static void rcw_toolbar_reconnect(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	gchar *filename = NULL;
	RemminaFile *remminafile = NULL;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	if (!cnnobj->remmina_file) return;

	if (cnnobj->remmina_file->filename) {
		filename = g_strdup(cnnobj->remmina_file->filename);
	} else {
		remminafile = remmina_file_dup(cnnobj->remmina_file);
	}

	if (cnnobj->connected) {
		rcw_toolbar_disconnect(toggle, cnnwin);
	}

	if (filename) {
		rcw_open_from_filename(filename);
		g_free(filename);
	} else if (remminafile) {
		rcw_open_from_file(remminafile);
	}
}

static void rcw_toolbar_grab(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean capture;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	capture = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));

	if (cnnobj->connected){
		remmina_file_set_int(cnnobj->remmina_file, "keyboard_grab", capture);
	}

	rcw_keyboard_grab_request(cnnobj, capture && cnnobj->connected);

	rco_update_toolbar(cnnobj);
}

static void rcw_toolbar_invert_scroll(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	gboolean capture;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring){
		return;
	}

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) {
		return;
	}

	capture = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
	
	if (cnnobj->connected && cnnobj->proto) {
		remmina_file_set_int(cnnobj->remmina_file, "invert_scroll", capture);
	}
}

static void rcw_update_pin(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	if (cnnwin->priv->pin_down)
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(cnnwin->priv->pin_button), REMMINA_APP_ID "-pin-down-symbolic");
	else
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(cnnwin->priv->pin_button), REMMINA_APP_ID "-pin-up-symbolic");
}

static void rcw_toolbar_pin(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	remmina_pref.toolbar_pin_down = cnnwin->priv->pin_down = !cnnwin->priv->pin_down;
	remmina_pref_save();
	rcw_update_pin(cnnwin);
}

static GtkWidget *
rcw_create_toolbar(RemminaConnectionWindow *cnnwin, gint mode, gboolean is_floating)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *toolbar;
	GtkToolItem *toolitem;
	GtkWidget *widget;
	GtkWidget *arrow;

	GdkDisplay *display;
	gint n_monitors;

	display = gdk_display_get_default();
	n_monitors = gdk_display_get_n_monitors(display);

	cnnobj = rcw_get_visible_cnnobj(cnnwin);

	priv->toolbar_is_reconfiguring = TRUE;

	toolbar = gtk_toolbar_new();
	gtk_widget_show(toolbar);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

	/* Main actions */

	if (is_floating){
			/* The pin button */
		GtkToolItem* pinbutton = gtk_toggle_tool_button_new();
		gtk_widget_show(GTK_WIDGET(pinbutton));
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), pinbutton, -1);
	#if GTK_CHECK_VERSION(3, 20, 0)
		gtk_widget_set_focus_on_click(GTK_WIDGET(pinbutton), FALSE);
	#else
		gtk_button_set_focus_on_click(GTK_BUTTON(pinbutton), FALSE);
	#endif
		gtk_widget_set_name(GTK_WIDGET(pinbutton), "remmina-pin-button");
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(pinbutton), remmina_pref.toolbar_pin_down);
		g_signal_connect(G_OBJECT(pinbutton), "clicked", G_CALLBACK(rcw_toolbar_pin), cnnwin);
		priv->pin_button = GTK_WIDGET(pinbutton);
		priv->pin_down = remmina_pref.toolbar_pin_down;
		rcw_update_pin(cnnwin);
	}

	/* Duplicate session */
	toolitem = gtk_tool_button_new(NULL, "Duplicate connection");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-duplicate-symbolic");
	gtk_tool_item_set_tooltip_text(toolitem, _("Duplicate current connection"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_duplicate), cnnwin);
	if (!cnnobj)
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);

	priv->toolitem_duplicate = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Auto-Fit */
	toolitem = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-fit-window-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Resize the window to fit in remote resolution"),
			remmina_pref.shortcutkey_autofit, 0);
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_autofit), cnnwin);
	priv->toolitem_autofit = toolitem;
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));


	/* Fullscreen toggle */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-fullscreen-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle fullscreen mode"),
			remmina_pref.shortcutkey_fullscreen, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	priv->toolitem_fullscreen = toolitem;
	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), mode != SCROLLED_WINDOW_MODE);
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_fullscreen), cnnwin);

	/* Fullscreen drop-down options */
	toolitem = gtk_tool_item_new();
	gtk_widget_show(GTK_WIDGET(toolitem));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	widget = gtk_toggle_button_new();
	gtk_widget_show(widget);
	gtk_container_set_border_width(GTK_CONTAINER(widget), 0);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
	if (remmina_pref.small_toolbutton)
		gtk_widget_set_name(widget, "remmina-small-button");

#else
	gtk_button_set_focus_on_click(GTK_BUTTON(widget), FALSE);
#endif
	gtk_container_add(GTK_CONTAINER(toolitem), widget);

#if GTK_CHECK_VERSION(3, 14, 0)
	arrow = gtk_image_new_from_icon_name(REMMINA_APP_ID "-pan-down-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
#else
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
#endif
	gtk_widget_show(arrow);
	gtk_container_add(GTK_CONTAINER(widget), arrow);
	g_signal_connect(G_OBJECT(widget), "toggled", G_CALLBACK(rcw_toolbar_fullscreen_option), cnnwin);
	priv->fullscreen_option_button = widget;
	if (mode == SCROLLED_WINDOW_MODE)
		gtk_widget_set_sensitive(GTK_WIDGET(widget), FALSE);

	/* Multi monitor */
	if (n_monitors > 1) {
		toolitem = gtk_toggle_tool_button_new();
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-multi-monitor-symbolic");
		rcw_set_tooltip(GTK_WIDGET(toolitem), _("Multi monitor"),
				remmina_pref.shortcutkey_multimon, 0);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
		gtk_widget_show(GTK_WIDGET(toolitem));
		g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_multi_monitor_mode), cnnwin);
		priv->toolitem_multimon = toolitem;
		if (!cnnobj)
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		else
			gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
							  remmina_file_get_int(cnnobj->remmina_file, "multimon", FALSE));
	}

	/* Dynamic Resolution Update */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-dynres-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle dynamic resolution update"),
			remmina_pref.shortcutkey_dynres, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_dynres), cnnwin);
	priv->toolitem_dynres = toolitem;

	/* Scaler button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-scale-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle scaled mode"), remmina_pref.shortcutkey_scale, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_scaled_mode), cnnwin);
	priv->toolitem_scale = toolitem;

	/* View only mode */
	// Widget set_sensitive setting is determined in rco_update_toolbar by the plugin protocol
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-password-reveal-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("View only mode"), remmina_pref.shortcutkey_viewonly, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_set_name(GTK_WIDGET(toolitem), "viewonly");
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_viewonly_mode), cnnwin);
	priv->toolitem_viewonly = toolitem;

	/* Scaler aspect ratio dropdown menu */
	toolitem = gtk_tool_item_new();
	gtk_widget_show(GTK_WIDGET(toolitem));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	widget = gtk_toggle_button_new();
	gtk_widget_show(widget);
	gtk_container_set_border_width(GTK_CONTAINER(widget), 0);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
#else
	gtk_button_set_focus_on_click(GTK_BUTTON(widget), FALSE);
#endif
	if (remmina_pref.small_toolbutton)
		gtk_widget_set_name(widget, "remmina-small-button");
	gtk_container_add(GTK_CONTAINER(toolitem), widget);
#if GTK_CHECK_VERSION(3, 14, 0)
	arrow = gtk_image_new_from_icon_name(REMMINA_APP_ID "-pan-down-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
#else
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
#endif
	gtk_widget_show(arrow);
	gtk_container_add(GTK_CONTAINER(widget), arrow);
	g_signal_connect(G_OBJECT(widget), "toggled", G_CALLBACK(rcw_toolbar_scaler_option), cnnwin);
	priv->scaler_option_button = widget;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Switch tabs */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-switch-page-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Switch tab pages"), remmina_pref.shortcutkey_prevtab,
			remmina_pref.shortcutkey_nexttab);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_switch_page), cnnwin);
	priv->toolitem_switch_page = toolitem;

	/* Grab keyboard button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-keyboard-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Grab all keyboard events"),
			remmina_pref.shortcutkey_grab, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_grab), cnnwin);
	priv->toolitem_grab = toolitem;

	if (!cnnobj)
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	else {
		const gchar *protocol = remmina_file_get_string(cnnobj->remmina_file, "protocol");
		if (g_strcmp0(protocol, "SFTP") == 0 || g_strcmp0(protocol, "SSH") == 0)
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	}

	/* Invert Scroll button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "input-mouse-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Invert scroll direction"),
			remmina_pref.shortcutkey_invert_scroll, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_invert_scroll), cnnwin);
	priv->toolitem_invert_scroll = toolitem;
	if (!cnnobj){
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	}

	/* Preferences */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-preferences-system-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Preferences"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Preferences"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_preferences), cnnwin);
	priv->toolitem_preferences = toolitem;

	/* Tools */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-system-run-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Tools"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Tools"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_tools), cnnwin);
	priv->toolitem_tools = toolitem;

	/* Credentials */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-insert-object-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Insert"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Insert"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_creds), cnnwin);
	priv->toolitem_creds = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	toolitem = gtk_tool_button_new(NULL, "_Screenshot");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-camera-photo-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Screenshot"), remmina_pref.shortcutkey_screenshot, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_screenshot), cnnwin);
	priv->toolitem_screenshot = toolitem;


	toolitem = gtk_tool_button_new(NULL, "_Edit_pref");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-document-properties-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Edit Preferences"), remmina_pref.shortcutkey_screenshot, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_edit_pref), cnnwin);
	priv->toolitem_edit_pref = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Minimize */
	toolitem = gtk_tool_button_new(NULL, "_Bottom");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-go-bottom-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Minimize window"), remmina_pref.shortcutkey_minimize, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_minimize), cnnwin);

	/* Reconnect */
	toolitem = gtk_tool_button_new(NULL, "_Reconnect");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-reconnect-symbolic");
	gtk_widget_set_tooltip_text(GTK_WIDGET(toolitem), _("Reconnect"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_reconnect), cnnwin);

	/* Disconnect */
	toolitem = gtk_tool_button_new(NULL, "_Disconnect");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), REMMINA_APP_ID "-disconnect-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Disconnect"), remmina_pref.shortcutkey_disconnect, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_disconnect), cnnwin);

	priv->toolbar_is_reconfiguring = FALSE;
	return toolbar;
}

static void rcw_place_toolbar(GtkToolbar *toolbar, GtkGrid *grid, GtkWidget *sibling, int toolbar_placement)
{
	/* Place the toolbar inside the grid and set its orientation */

	if (toolbar_placement == TOOLBAR_PLACEMENT_LEFT || toolbar_placement == TOOLBAR_PLACEMENT_RIGHT)
		gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar), GTK_ORIENTATION_VERTICAL);
	else
		gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar), GTK_ORIENTATION_HORIZONTAL);


	switch (toolbar_placement) {
	case TOOLBAR_PLACEMENT_TOP:
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_TOP, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_RIGHT:
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_RIGHT, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_BOTTOM:
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_BOTTOM, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_LEFT:
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_LEFT, 1, 1);
		break;
	}
}

static void rco_update_toolbar(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkToolItem *toolitem;
	gboolean bval, dynres_avail, scale_avail, viewonly_avail;
	gboolean test_floating_toolbar;
	RemminaScaleMode scalemode;

	priv->toolbar_is_reconfiguring = TRUE;

	rco_update_toolbar_autofit_button(cnnobj);

	toolitem = priv->toolitem_switch_page;
	bval = (gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)) > 1);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval);

	if (cnnobj->remmina_file->filename)
		gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_duplicate), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_duplicate), FALSE);

	scalemode = get_current_allowed_scale_mode(cnnobj, &dynres_avail, &scale_avail);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_dynres), dynres_avail && cnnobj->connected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_scale), scale_avail && cnnobj->connected);

	viewonly_avail = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_viewonly), viewonly_avail && cnnobj->connected);
	if (viewonly_avail && cnnobj->connected) {
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_viewonly), remmina_file_get_int(cnnobj->remmina_file, "viewonly", 0)==0? FALSE: TRUE);
	}

	switch (scalemode) {
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		break;
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), TRUE && cnnobj->connected);
		break;
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), TRUE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		break;
	}

	/* REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON */
	toolitem = priv->toolitem_multimon;
	if (toolitem) {
		gint hasmultimon = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
										 REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON);
		gboolean is_multimon = remmina_file_get_int(cnnobj->remmina_file, "multimon", FALSE) && hasmultimon;

		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), is_multimon);
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), hasmultimon);
		if (is_multimon) {
			gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_scale), FALSE);
			gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		}
	}

	toolitem = priv->toolitem_grab;
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
					  remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE));

	toolitem = priv->toolitem_invert_scroll;
	if (toolitem) {
		gint hasinvertscroll = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
										 REMMINA_PROTOCOL_FEATURE_TYPE_INVERT_SCROLL);
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), hasinvertscroll && cnnobj->connected);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
						  remmina_file_get_int(cnnobj->remmina_file, "invert_scroll", FALSE));
	}
	const gchar *protocol = remmina_file_get_string(cnnobj->remmina_file, "protocol");
	if (g_strcmp0(protocol, "SFTP") == 0 || g_strcmp0(protocol, "SSH") == 0) {
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), FALSE);
		remmina_file_set_int(cnnobj->remmina_file, "keyboard_grab", FALSE);
	}

	toolitem = priv->toolitem_preferences;
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
	bval = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_PREF);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval && cnnobj->connected);

	toolitem = priv->toolitem_tools;
	bval = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_TOOL);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval && cnnobj->connected);

	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_screenshot), cnnobj->connected);

	if (cnnobj->remmina_file != NULL && cnnobj->remmina_file->filename != NULL) {
		gtk_widget_show(GTK_WIDGET(cnnobj->cnnwin->priv->toolitem_edit_pref));
	}

	{
		gchar *title = g_strdup_printf("%s — UniDeskBrowser", remmina_file_get_string(cnnobj->remmina_file, "name"));
		gtk_window_set_title(GTK_WINDOW(cnnobj->cnnwin), title);
		g_free(title);
	}

	test_floating_toolbar = (priv->floating_toolbar_widget != NULL);

	if (test_floating_toolbar) {
		const gchar *str = remmina_file_get_string(cnnobj->remmina_file, "name");
		const gchar *format;
		GdkRGBA rgba;
		gchar *bg;

		bg = g_strdup(remmina_pref.grab_color);
		if (!gdk_rgba_parse(&rgba, bg)) {
			REMMINA_DEBUG("%s cannot be parsed as a color", bg);
			g_free(bg);
			bg = g_strdup("#00FF00");
		} else {
			REMMINA_DEBUG("Using %s as background color", bg);
		}

// gtk_widget_override_background_color is deprecated, and this will be handled with GTK4 migration
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		if (remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE)) {
			if (remmina_pref_get_boolean("grab_color_switch")) {
				gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, &rgba);
				format = g_strconcat("<span bgcolor=\"", bg, "\" size=\"large\"><b>(G: ON) - \%s</b></span>", NULL);
			} else {
				gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, NULL);
				format = "<big><b>(G: ON) - \%s</b></big>";
			}
		} else {
			gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, NULL);
			format = "<big><b>(G:OFF) - \%s</b></big>";
		}
		gchar *markup;
#pragma clang diagnostic pop

		markup = g_markup_printf_escaped(format, str);
		gtk_label_set_markup(GTK_LABEL(priv->floating_toolbar_label), markup);
		g_free(markup);
		g_free(bg);
	}

	priv->toolbar_is_reconfiguring = FALSE;
}

static void rcw_set_toolbar_visibility(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->view_mode == SCROLLED_WINDOW_MODE) {
		if (priv->is_main) {
			rcw_update_empty_state(cnnwin);
			return;
		}
		if (remmina_pref.hide_connection_toolbar)
			gtk_widget_hide(priv->toolbar);
		else
			gtk_widget_show(priv->toolbar);
	}
}

#if DEBUG_KB_GRABBING
static void print_crossing_event(GdkEventCrossing *event) {
	const char*event_name;
	
	switch (event->detail) {
	case GDK_NOTIFY_ANCESTOR: event_name="GDK_NOTIFY_ANCESTOR"; break;
	case GDK_NOTIFY_VIRTUAL: event_name="GDK_NOTIFY_VIRTUAL"; break;
	case GDK_NOTIFY_NONLINEAR: event_name="GDK_NOTIFY_NONLINEAR"; break;
	case GDK_NOTIFY_NONLINEAR_VIRTUAL: event_name="GDK_NOTIFY_NONLINEAR_VIRTUAL"; break;
	case GDK_NOTIFY_UNKNOWN: event_name="GDK_NOTIFY_UNKNOWN"; break;
	case GDK_NOTIFY_INFERIOR: event_name="GDK_NOTIFY_INFERIOR"; break;
	default: event_name="unknown";
	}
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- Crossing event detail: %s", event_name);
	switch (event->mode) {
	case GDK_CROSSING_NORMAL: event_name="GDK_CROSSING_NORMAL"; break;
	case GDK_CROSSING_GRAB: event_name="GDK_CROSSING_GRAB"; break;
	case GDK_CROSSING_UNGRAB: event_name="GDK_CROSSING_UNGRAB"; break;
	case GDK_CROSSING_GTK_GRAB: event_name="GDK_CROSSING_GTK_GRAB"; break;
	case GDK_CROSSING_GTK_UNGRAB: event_name="GDK_CROSSING_GTK_UNGRAB"; break;
	case GDK_CROSSING_STATE_CHANGED: event_name="GDK_CROSSING_STATE_CHANGED"; break;
	case GDK_CROSSING_TOUCH_BEGIN: event_name="GDK_CROSSING_TOUCH_BEGIN"; break;
	case GDK_CROSSING_TOUCH_END: event_name="GDK_CROSSING_TOUCH_END"; break;
	case GDK_CROSSING_DEVICE_SWITCH: event_name="GDK_CROSSING_DEVICE_SWITCH"; break;
	default: event_name="unknown";
	}
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- Crossing event mode=%s", event_name);
}
#endif

static gboolean toolbar_display(gpointer user_data)
{
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)user_data;
	if (cnnwin != NULL && cnnwin->priv != NULL) {
		cnnwin->priv->ftb_show_eventsource = 0;
		rcw_floating_toolbar_show(cnnwin, TRUE);
	}
	return G_SOURCE_REMOVE;
}

static gboolean rcw_floating_toolbar_on_enter(GtkWidget *widget, GdkEventCrossing *event,
					      RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	if (cnnwin == NULL || cnnwin->priv == NULL)
		return TRUE;

	GtkStyleContext *style_context = gtk_widget_get_style_context(cnnwin->priv->overlay_ftb_fr);
	GtkBorder margin;
	gtk_style_context_get_margin(style_context, gtk_style_context_get_state(style_context), &margin);
	if (cnnwin->priv->ftb_move_horiz >= 0 && event->x < cnnwin->priv->ftb_move_horiz + margin.left) {
		// the toolbar must not pop (the mouse pointer is not over the visible area, but on the left)
		return TRUE;
	}
	if (remmina_pref.fullscreen_toolbar_delay > 0) {
		if (cnnwin->priv->ftb_show_eventsource == 0) {
			cnnwin->priv->ftb_show_eventsource = g_timeout_add(
				remmina_pref.fullscreen_toolbar_delay * 1000,
				G_SOURCE_FUNC(toolbar_display),
				cnnwin);
		}
	} else {
		rcw_floating_toolbar_show(cnnwin, TRUE);
	}

	return TRUE;
}

static gboolean rcw_floating_toolbar_on_leave(GtkWidget *widget, GdkEventCrossing *event,
					      RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	if (cnnwin == NULL || cnnwin->priv == NULL)
		return TRUE;

	if (cnnwin->priv->ftb_show_eventsource) {
		g_source_remove(cnnwin->priv->ftb_show_eventsource);
		cnnwin->priv->ftb_show_eventsource = 0;
	}

	if (event->detail != GDK_NOTIFY_INFERIOR) {
		rcw_floating_toolbar_show(cnnwin, FALSE);
	}

	return TRUE;
}


static gboolean rcw_on_enter_notify_event(GtkWidget *widget, GdkEventCrossing *event,
					  gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: enter-notify-event on rcw received");
	print_crossing_event(event);
#endif
	return FALSE;
}



static gboolean rcw_on_leave_notify_event(GtkWidget *widget, GdkEventCrossing *event,
					  gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: leave-notify-event on rcw received");
	print_crossing_event(event);
#endif

	if (event->mode != GDK_CROSSING_NORMAL && event->mode != GDK_CROSSING_UNGRAB) {
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: ignored because mode is not GDK_CROSSING_NORMAL GDK_CROSSING_UNGRAB");
#endif
		return FALSE;
	}

	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}

	RemminaConnectionObject* cnnobj = rcw_get_visible_cnnobj(cnnwin);
	rcw_keyboard_grab_request(cnnobj, event->mode == GDK_CROSSING_UNGRAB);

	return FALSE;
}


static gboolean rco_leave_protocol_widget(GtkWidget *widget, GdkEventCrossing *event,
					  RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: received leave event on RCO.");
	print_crossing_event(event);
#endif

	if (cnnobj->cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnobj->cnnwin->priv->delayed_grab_eventsourceid);
		cnnobj->cnnwin->priv->delayed_grab_eventsourceid = 0;
	}

	/* Ungrab only if the leave is due to normal mouse motion and not to an inferior */
	if (event->mode == GDK_CROSSING_NORMAL && event->detail != GDK_NOTIFY_INFERIOR)
		rcw_keyboard_grab_request(cnnobj, FALSE);
	if (event->mode == GDK_CROSSING_UNGRAB)
		rcw_keyboard_grab_request(cnnobj, FALSE);
		
	return FALSE;
}


static gboolean rco_enter_protocol_widget(GtkWidget *widget, GdkEventCrossing *event,
				   RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: enter on protocol widget event received");
	print_crossing_event(event);
#endif

	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	if (!priv->sticky && event->mode == GDK_CROSSING_NORMAL)
		rcw_floating_toolbar_show(cnnobj->cnnwin, FALSE);
	
	if (!cnnobj->connected) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: Not connected yet, abort");
#endif
		return FALSE;
	}
	rcw_keyboard_grab_request(cnnobj, event->mode != GDK_CROSSING_UNGRAB);
	rcw_set_active_pane(cnnobj->cnnwin, cnnobj);

	return FALSE;
}

static void rcw_focus_in(RemminaConnectionWindow *cnnwin)
{
	/* This function is the default signal handler for focus-in-event,
	 * but can also be called after a window focus state change event
	 * from rcw_state_event(). So expect to be called twice
	 * when cnnwin gains the focus */

	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	/* Do not grab the keyboard while the user works in the sidebar */
	if (rcw_widget_in_sidebar(cnnwin, gtk_window_get_focus(GTK_WINDOW(cnnwin))))
		return;

	rcw_keyboard_grab_request(cnnobj, TRUE);
}

static void rcw_focus_out(RemminaConnectionWindow *cnnwin)
{
	/* This function is the default signal handler for focus-out-event,
	 * but can also be called after a window focus state change event
	 * from rcw_state_event(). So expect to be called twice
	 * when cnnwin loses the focus */

	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	rcw_keyboard_grab_request(cnnobj, FALSE);

	cnnwin->priv->hostkey_activated = FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container))
		remmina_scrolled_viewport_remove_motion(REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container));

	if (cnnobj->proto && cnnobj->scrolled_container)
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_UNFOCUS, 0);
}

static gboolean
rcw_floating_toolbar_hide(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->hidetb_eventsource = 0;
	rcw_floating_toolbar_show(cnnwin, FALSE);
	return G_SOURCE_REMOVE;
}

static gboolean rcw_floating_toolbar_on_scroll(GtkWidget *widget, GdkEventScroll *event,
					       RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	int opacity;

	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return TRUE;

	opacity = remmina_file_get_int(cnnobj->remmina_file, "toolbar_opacity", 0);
	switch (event->direction) {
	case GDK_SCROLL_UP:
		if (opacity > 0) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity - 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
	case GDK_SCROLL_DOWN:
		if (opacity < TOOLBAR_OPACITY_LEVEL) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity + 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
#if GTK_CHECK_VERSION(3, 4, 0)
	case GDK_SCROLL_SMOOTH:
		if (event->delta_y < 0 && opacity > 0) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity - 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		if (event->delta_y > 0 && opacity < TOOLBAR_OPACITY_LEVEL) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity + 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
#endif
	default:
		break;
	}
	return TRUE;
}

static gboolean rcw_after_configure_scrolled(gpointer user_data)
{
	TRACE_CALL(__func__);
	gint width, height;
	GdkWindowState s;
	gint ipg, npages;
	RemminaConnectionWindow *cnnwin;

	cnnwin = (RemminaConnectionWindow *)user_data;

	if (!cnnwin || !cnnwin->priv)
		return FALSE;

	s = gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(cnnwin)));

	if (cnnwin->priv->is_main) {
		/* The application window geometry is a global preference */
		if (s & GDK_WINDOW_STATE_MAXIMIZED) {
			remmina_pref.main_maximize = TRUE;
		} else {
			gtk_window_get_size(GTK_WINDOW(cnnwin), &width, &height);
			remmina_pref.main_width = width;
			remmina_pref.main_height = height;
			remmina_pref.main_maximize = FALSE;
		}
		cnnwin->priv->acs_eventsourceid = 0;
		return FALSE;
	}

	/* Changed window_maximize, window_width and window_height for all
	 * connections inside the notebook */
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(cnnwin->priv->notebook));
	for (ipg = 0; ipg < npages; ipg++) {
		RemminaConnectionObject *cnnobj;
		cnnobj = g_object_get_data(
			G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(cnnwin->priv->notebook), ipg)),
			"cnnobj");
		if (!cnnobj || !cnnobj->remmina_file)
			continue;
		if (s & GDK_WINDOW_STATE_MAXIMIZED) {
			remmina_file_set_int(cnnobj->remmina_file, "window_maximize", TRUE);
		} else {
			gtk_window_get_size(GTK_WINDOW(cnnobj->cnnwin), &width, &height);
			remmina_file_set_int(cnnobj->remmina_file, "window_width", width);
			remmina_file_set_int(cnnobj->remmina_file, "window_height", height);
			remmina_file_set_int(cnnobj->remmina_file, "window_maximize", FALSE);
		}
	}
	cnnwin->priv->acs_eventsourceid = 0;
	return FALSE;
}

static gboolean rcw_on_configure(GtkWidget *widget, GdkEventConfigure *event,
				 gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	RemminaConnectionObject *cnnobj;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return FALSE;

	cnnwin = (RemminaConnectionWindow *)widget;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	if (cnnwin->priv->acs_eventsourceid) {
		g_source_remove(cnnwin->priv->acs_eventsourceid);
		cnnwin->priv->acs_eventsourceid = 0;
	}

	if (gtk_widget_get_window(GTK_WIDGET(cnnwin))
	    && cnnwin->priv->view_mode == SCROLLED_WINDOW_MODE)
		/* Under GNOME Shell we receive this configure_event BEFORE a window
		 * is really unmaximized, so we must read its new state and dimensions
		 * later, not now */
		cnnwin->priv->acs_eventsourceid = g_timeout_add(500, rcw_after_configure_scrolled, cnnwin);

	if (cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		/* Notify window of change so that scroll border can be hidden or shown if needed */
		rco_check_resize(cnnobj);
	return FALSE;
}

static void rcw_create_floating_toolbar(RemminaConnectionWindow *cnnwin, gint mode)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *ftb_widget;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *tb;


	/* A widget to be used for GtkOverlay for GTK >= 3.10 */
	ftb_widget = gtk_event_box_new();

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(vbox);

	gtk_container_add(GTK_CONTAINER(ftb_widget), vbox);

	tb = rcw_create_toolbar(cnnwin, mode, TRUE);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show(hbox);

	label = gtk_label_new("");
	gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
	gtk_widget_show(label);

	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

	priv->floating_toolbar_label = label;

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT) {
			if (remmina_pref.fullscreen_toolbar_visibility != FLOATING_TOOLBAR_VISIBILITY_MINIMAL){
				gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
				gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			}
			else{
				gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			}
	} else {
		if (remmina_pref.fullscreen_toolbar_visibility != FLOATING_TOOLBAR_VISIBILITY_MINIMAL){
			gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		}
		else{
			gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
		}
	}

	priv->floating_toolbar_widget = ftb_widget;
	gtk_widget_show(ftb_widget);
}

static void rcw_toolbar_place_signal(RemminaConnectionWindow *cnnwin, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;

	priv = cnnwin->priv;
	/* Detach old toolbar widget and reattach in new position in the grid */
	if (priv->toolbar && priv->grid) {
		g_object_ref(priv->toolbar);
		gtk_container_remove(GTK_CONTAINER(priv->grid), priv->toolbar);
		rcw_place_toolbar(GTK_TOOLBAR(priv->toolbar), GTK_GRID(priv->grid),
				  priv->content_stack ? priv->content_stack : GTK_WIDGET(priv->notebook), remmina_pref.toolbar_placement);
		g_object_unref(priv->toolbar);
	}
}


static void rcw_init(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;

	priv = g_new0(RemminaConnectionWindowPriv, 1);
	cnnwin->priv = priv;

	priv->view_mode = SCROLLED_WINDOW_MODE;

	priv->multimon_areas = NULL;
	priv->multimon_main_monitor = -1;

	priv->floating_toolbar_opacity = 1.0;
	priv->grab_pending_status = FALSE;
	priv->fss_view_mode = VIEWPORT_FULLSCREEN_MODE;
	priv->ss_width = 640;
	priv->ss_height = 480;
	priv->ss_maximized = FALSE;

	priv->ftb_move_horiz = 0;
	priv->ftb_move_vert = 0;

	/* Meta+W closes the current tab, Meta+N opens a new window, Meta+T goes to the home tab (the
	 * page of a new tab). Window accelerators come before the focused widget, so they work in a
	 * remote session too, where every other key goes to the remote side */
	GtkAccelGroup *accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(cnnwin), accel_group);
	gtk_accel_group_connect(accel_group, GDK_KEY_w, GDK_SUPER_MASK, 0,
				g_cclosure_new(G_CALLBACK(rcw_accel_close_tab), cnnwin, NULL));
	gtk_accel_group_connect(accel_group, GDK_KEY_n, GDK_SUPER_MASK, 0,
				g_cclosure_new(G_CALLBACK(rcw_accel_new_window), cnnwin, NULL));
	gtk_accel_group_connect(accel_group, GDK_KEY_t, GDK_SUPER_MASK, 0,
				g_cclosure_new(G_CALLBACK(rcw_accel_home_tab), cnnwin, NULL));
	gtk_accel_group_connect(accel_group, GDK_KEY_t, GDK_SUPER_MASK | GDK_SHIFT_MASK, 0,
				g_cclosure_new(G_CALLBACK(rcw_accel_reopen_tab), cnnwin, NULL));
	g_object_unref(accel_group);

	remmina_widget_pool_register(GTK_WIDGET(cnnwin));
}

static gboolean rcw_focus_in_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: RCW focus-in-event received");
#endif
	rcw_focus_in((RemminaConnectionWindow *)widget);
	return FALSE;
}

static gboolean rcw_focus_out_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: RCW focus-out-event received");
#endif
	rcw_focus_out((RemminaConnectionWindow *)widget);
	return FALSE;
}

#if DEBUG_KB_GRABBING
static void print_gdk_window_state_event(const char*message, GdkWindowState state)
{
	static const struct {
    GdkWindowState flag;
    const char *name;
	} gdk_window_state_enum[] = {
		{ GDK_WINDOW_STATE_WITHDRAWN, "GDK_WINDOW_STATE_WITHDRAWN" },
		{ GDK_WINDOW_STATE_ICONIFIED, "GDK_WINDOW_STATE_ICONIFIED" },
		{ GDK_WINDOW_STATE_MAXIMIZED, "GDK_WINDOW_STATE_MAXIMIZED" },
		{ GDK_WINDOW_STATE_STICKY,    "GDK_WINDOW_STATE_STICKY" },
		{ GDK_WINDOW_STATE_FULLSCREEN,"GDK_WINDOW_STATE_FULLSCREEN" },
		{ GDK_WINDOW_STATE_ABOVE,     "GDK_WINDOW_STATE_ABOVE" },
		{ GDK_WINDOW_STATE_BELOW,     "GDK_WINDOW_STATE_BELOW" },
		{ GDK_WINDOW_STATE_FOCUSED,   "GDK_WINDOW_STATE_FOCUSED" },
		{ GDK_WINDOW_STATE_TILED,     "GDK_WINDOW_STATE_TILED" },
		{ GDK_WINDOW_STATE_TOP_TILED, "GDK_WINDOW_STATE_TOP_TILED" },
		{ GDK_WINDOW_STATE_TOP_RESIZABLE,"GDK_WINDOW_STATE_TOP_RESIZABLE" },
		{ GDK_WINDOW_STATE_RIGHT_TILED,"GDK_WINDOW_STATE_RIGHT_TILED" },
		{ GDK_WINDOW_STATE_RIGHT_RESIZABLE,"GDK_WINDOW_STATE_RIGHT_RESIZABLE" },
		{ GDK_WINDOW_STATE_BOTTOM_TILED,"GDK_WINDOW_STATE_BOTTOM_TILED" },
		{ GDK_WINDOW_STATE_BOTTOM_RESIZABLE,"GDK_WINDOW_STATE_BOTTOM_RESIZABLE" },
		{ GDK_WINDOW_STATE_LEFT_TILED,"GDK_WINDOW_STATE_LEFT_TILED" },
		{ GDK_WINDOW_STATE_LEFT_RESIZABLE,"GDK_WINDOW_STATE_LEFT_RESIZABLE" }
	};
    int first = 1;
    size_t table_size = sizeof(gdk_window_state_enum) / sizeof(gdk_window_state_enum[0]);
	GString*state_str = g_string_new("");

    for (size_t i = 0; i < table_size; ++i) {
        if (state & gdk_window_state_enum[i].flag) {
            if (!first) state_str = g_string_append(state_str, "|");
            state_str = g_string_append(state_str, gdk_window_state_enum[i].name);
            first = 0;
        }
    }
    if (first) {
        state_str = g_string_append(state_str, "GDK_WINDOW_STATE_NONE");
    }
	REMMINA_DEBUG(message, state_str->str);
	g_string_free(state_str, TRUE);
}
#endif

static gboolean rcw_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return FALSE;

#if DEBUG_KB_GRABBING
	print_gdk_window_state_event("DEBUG_KB_GRABBING: changed_mask=%s", event->changed_mask);
	print_gdk_window_state_event("DEBUG_KB_GRABBING: new_window_state=%s", event->new_window_state);
#endif

	// This will trigger iconify/deiconify on other multimonitor windows
	if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED &&
		event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN &&
		event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
		rcw_iconify_widget_areas(cnnwin);
	} else if (event->changed_mask & GDK_WINDOW_STATE_FOCUSED &&
		       event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN &&
			   event->new_window_state & GDK_WINDOW_STATE_FOCUSED) {
 		// This is an ugly trick to keep the main window above even after lose focus every time (otherwise this works only one)
		gtk_window_set_keep_above(GTK_WINDOW(cnnwin), FALSE);
	    gtk_window_set_keep_above(GTK_WINDOW(cnnwin), TRUE);
		// restore other multimonitor windows if any
		rcw_restore_widget_areas(cnnwin);
	}
	if (event->changed_mask & GDK_WINDOW_STATE_FOCUSED) {
		if (event->new_window_state & GDK_WINDOW_STATE_FOCUSED)
			rcw_focus_in((RemminaConnectionWindow *)widget);
		else
			rcw_focus_out((RemminaConnectionWindow *)widget);
	}

	return FALSE;
}

static gboolean rcw_map_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);



	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;
	RemminaConnectionObject *cnnobj;
	RemminaProtocolWidget *gp;

	if (cnnwin->priv->toolbar_is_reconfiguring) return FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);
	REMMINA_DEBUG("Mapping: %s", gtk_widget_get_name(widget));
	if (remmina_protocol_widget_map_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");
	return FALSE;
}

static gboolean rcw_unmap_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;
	RemminaConnectionObject *cnnobj;
	RemminaProtocolWidget *gp;

	if (cnnwin->priv->toolbar_is_reconfiguring) return FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);
	REMMINA_DEBUG("Unmapping: %s", gtk_widget_get_name(widget));
	if (remmina_protocol_widget_unmap_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");
	return FALSE;
}

static gboolean rcw_map_event_fullscreen(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	gint target_monitor;

	REMMINA_DEBUG("Mapping: %s", gtk_widget_get_name(widget));

	if (!REMMINA_IS_CONNECTION_WINDOW(widget)) {
		REMMINA_DEBUG("Remmina Connection Window undefined, cannot go fullscreen");
		return FALSE;
	}

	cnnobj = rcw_get_visible_cnnobj((RemminaConnectionWindow *)widget);
	if (!cnnobj) {
		REMMINA_DEBUG("Remmina Connection Object undefined, cannot go fullscreen");
		return FALSE;
	}

	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	if (!gp)
		REMMINA_DEBUG("Remmina Protocol Widget undefined, cannot go fullscreen");

	if (remmina_protocol_widget_get_multimon(gp) >= 1) {
		REMMINA_DEBUG("Fullscreen on main monitor");
		gtk_window_fullscreen_on_monitor(GTK_WINDOW(widget), gtk_window_get_screen(GTK_WINDOW(widget)), cnnobj->cnnwin->priv->multimon_main_monitor);
		rcw_show_widget_areas(cnnobj->cnnwin);
	} else {
		REMMINA_DEBUG("Fullscreen on one monitor");

		target_monitor = GPOINTER_TO_INT(data);

#if GTK_CHECK_VERSION(3, 18, 0)
		if (remmina_pref.fullscreen_on_auto) {
			if (target_monitor == FULL_SCREEN_TARGET_MONITOR_UNDEFINED)
				gtk_window_fullscreen(GTK_WINDOW(widget));
			else
				gtk_window_fullscreen_on_monitor(GTK_WINDOW(widget), gtk_window_get_screen(GTK_WINDOW(widget)),
								target_monitor);
		} else {
			REMMINA_DEBUG("Fullscreen managed by WM or by the user, as per settings");
			gtk_window_fullscreen(GTK_WINDOW(widget));
		}
#else
		REMMINA_DEBUG("Cannot fullscreen on a specific monitor, feature available from GTK 3.18");
		gtk_window_fullscreen(GTK_WINDOW(widget));
#endif
	}

	if (remmina_protocol_widget_map_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");

	return FALSE;
}

static RemminaConnectionWindow *
rcw_new(gboolean fullscreen, int full_screen_target_monitor)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;

	cnnwin = RCW(g_object_new(REMMINA_TYPE_CONNECTION_WINDOW, NULL));
	cnnwin->priv->on_delete_confirm_mode = RCW_ONDELETE_CONFIRM_IF_2_OR_MORE;
	cnnwin->priv->multimon_main_monitor = full_screen_target_monitor;

	if (fullscreen)
		/* Put the window in fullscreen after it is mapped to have it appear on the same monitor */
		g_signal_connect(G_OBJECT(cnnwin), "map-event", G_CALLBACK(rcw_map_event_fullscreen), GINT_TO_POINTER(full_screen_target_monitor));
	else
		g_signal_connect(G_OBJECT(cnnwin), "map-event", G_CALLBACK(rcw_map_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "unmap-event", G_CALLBACK(rcw_unmap_event), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(cnnwin), 0);
	g_signal_connect(G_OBJECT(cnnwin), "toolbar-place", G_CALLBACK(rcw_toolbar_place_signal), NULL);

	g_signal_connect(G_OBJECT(cnnwin), "delete-event", G_CALLBACK(rcw_delete_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "destroy", G_CALLBACK(rcw_destroy), NULL);

	/* Under Xorg focus-in-event and focus-out-event don’t work when keyboard is grabbed
	 * via gdk_device_grab. So we listen for window-state-event to detect focus in and focus out.
	 * But we must also listen focus-in-event and focus-out-event because some
	 * window managers missing _NET_WM_STATE_FOCUSED hint, does not update the window state
	 * in case of focus change */
	g_signal_connect(G_OBJECT(cnnwin), "window-state-event", G_CALLBACK(rcw_state_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "focus-in-event", G_CALLBACK(rcw_focus_in_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "focus-out-event", G_CALLBACK(rcw_focus_out_event), NULL);

	g_signal_connect(G_OBJECT(cnnwin), "enter-notify-event", G_CALLBACK(rcw_on_enter_notify_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "leave-notify-event", G_CALLBACK(rcw_on_leave_notify_event), NULL);


	g_signal_connect(G_OBJECT(cnnwin), "configure_event", G_CALLBACK(rcw_on_configure), NULL);

	return cnnwin;
}

void rcw_grab_focus(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (GTK_IS_WIDGET(cnnobj->proto))
		remmina_protocol_widget_grab_focus(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
}

static GtkWidget *nb_find_page_by_cnnobj(GtkNotebook *notebook, RemminaConnectionObject *cnnobj)
{
	gint i, np;
	GtkWidget *found_page, *pg;

	if (cnnobj == NULL || cnnobj->cnnwin == NULL || cnnobj->cnnwin->priv == NULL)
		return NULL;
	if (cnnobj->tab_page && GTK_IS_WIDGET(cnnobj->tab_page)){
		return cnnobj->tab_page;
	}
	found_page = NULL;
	np = gtk_notebook_get_n_pages(cnnobj->cnnwin->priv->notebook);
	for (i = 0; i < np; i++) {
		pg = gtk_notebook_get_nth_page(cnnobj->cnnwin->priv->notebook, i);
		if (g_object_get_data(G_OBJECT(pg), "cnnobj") == cnnobj) {
			found_page = pg;
			break;
		}
		GList *list = g_object_get_data(G_OBJECT(pg), "cnnobj-list");
		if (g_list_find(list, cnnobj)) {
			found_page = pg;
			break;
		}
	}

	return found_page;
}


static void rco_closewin(RemminaProtocolWidget *gp)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	GtkWidget *page_to_remove;

	if (!cnnobj || !cnnobj->cnnwin || !G_IS_OBJECT(cnnobj->cnnwin)) {
	    REMMINA_WARNING("Invalid cnnobj or cnnwin in rco_closewin");
	    return;
	}

	if (cnnobj && cnnobj->scrolled_container && REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container)) {
		REMMINA_DEBUG("deleting motion");
		remmina_scrolled_viewport_remove_motion(REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container));
	}

	if (cnnobj && cnnobj->cnnwin) {
		page_to_remove = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
		if (page_to_remove) {
			GtkWidget *parent = cnnobj->scrolled_container ? gtk_widget_get_parent(cnnobj->scrolled_container) : NULL;
			GList *list = g_object_get_data(G_OBJECT(page_to_remove), "cnnobj-list");
			list = g_list_remove(list, cnnobj);
			g_object_set_data(G_OBJECT(page_to_remove), "cnnobj-list", list);

			if (parent && GTK_IS_PANED(parent)) {
				// This connection is inside a split pane. Collapse the paned and promote sibling 
				GtkWidget *child1 = gtk_paned_get_child1(GTK_PANED(parent));
				GtkWidget *child2 = gtk_paned_get_child2(GTK_PANED(parent));
				GtkWidget *sibling = (child1 == cnnobj->scrolled_container) ? child2 : child1;
				GtkWidget *grandparent = gtk_widget_get_parent(parent);

				if (sibling) {
					g_object_ref(sibling);
					if (child1) gtk_container_remove(GTK_CONTAINER(parent), child1);
					if (child2) gtk_container_remove(GTK_CONTAINER(parent), child2);

					if (grandparent && GTK_IS_BOX(grandparent)) {
						gtk_container_remove(GTK_CONTAINER(grandparent), parent);
						gtk_box_pack_start(GTK_BOX(grandparent), sibling, TRUE, TRUE, 0);
					} else if (grandparent && GTK_IS_PANED(grandparent)) {
						if (gtk_paned_get_child1(GTK_PANED(grandparent)) == parent) {
							gtk_container_remove(GTK_CONTAINER(grandparent), parent);
							gtk_paned_pack1(GTK_PANED(grandparent), sibling, TRUE, FALSE);
						} else {
							gtk_container_remove(GTK_CONTAINER(grandparent), parent);
							gtk_paned_pack2(GTK_PANED(grandparent), sibling, TRUE, FALSE);
						}
					}
					g_object_unref(sibling);
				}

				cnnobj->aspectframe = NULL;
				cnnobj->viewport = NULL;
				cnnobj->scrolled_container = NULL;

				if (g_object_get_data(G_OBJECT(page_to_remove), "active-cnnobj") == cnnobj) {
					RemminaConnectionObject *next_active = list ? (RemminaConnectionObject *)list->data : NULL;
					rcw_set_active_pane(cnnobj->cnnwin, next_active);
				} else {
					if (g_list_length(list) <= 1 && list) {
						RemminaConnectionObject *rem = (RemminaConnectionObject *)list->data;
						if (rem && rem->scrolled_container) {
							GtkStyleContext *ctx = gtk_widget_get_style_context(rem->scrolled_container);
							gtk_style_context_remove_class(ctx, "remmina-active-terminal-pane");
						}
					}
				}
			} else {
				// Last pane in tab, remove notebook page
				gtk_notebook_remove_page(
					cnnobj->cnnwin->priv->notebook,
					gtk_notebook_page_num(cnnobj->cnnwin->priv->notebook, page_to_remove));
				cnnobj->aspectframe = NULL;
				cnnobj->viewport = NULL;
				cnnobj->scrolled_container = NULL;
			}
		}
	}
	if (cnnobj) {
		if (cnnobj->cnnwin && cnnobj->cnnwin->priv && cnnobj->cnnwin->priv->grab_realize_source_id) {
			g_source_remove(cnnobj->cnnwin->priv->grab_realize_source_id);
			cnnobj->cnnwin->priv->grab_realize_source_id = 0;
		}
		cnnobj->remmina_file = NULL;
		g_free(cnnobj);
		gp->cnnobj = NULL;
	}

	remmina_application_condexit(REMMINA_CONDEXIT_ONDISCONNECT);
}

static void rco_on_close_button_clicked(GtkButton *button, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	GtkWidget *page = cnnobj->tab_page ? cnnobj->tab_page : (cnnobj->cnnwin && cnnobj->cnnwin->priv ? nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj) : NULL);
	if (page) {
		GList *list = g_object_get_data(G_OBJECT(page), "cnnobj-list");
		if (list && g_list_length(list) > 1) {
			GList *copy = g_list_copy(list);
			for (GList *l = copy; l != NULL; l = l->next) {
				RemminaConnectionObject *co = (RemminaConnectionObject *)l->data;
				if (co && REMMINA_IS_PROTOCOL_WIDGET(co->proto)) {
					if (!remmina_protocol_widget_is_closed((RemminaProtocolWidget *)co->proto))
						remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(co->proto));
					else
						rco_closewin((RemminaProtocolWidget *)co->proto);
				}
			}
			g_list_free(copy);
			return;
		}
	}
	if (REMMINA_IS_PROTOCOL_WIDGET(cnnobj->proto)) {
		if (!remmina_protocol_widget_is_closed((RemminaProtocolWidget *)cnnobj->proto))
			remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
		else
			rco_closewin((RemminaProtocolWidget *)cnnobj->proto);
	}
}

/* Browser-like tab title: GtkNotebook allocates tabs their *minimum* width, and an
 * ellipsized label's minimum is three characters, so the minimum has to follow the
 * text length (capped) for the title to be readable. */
#define RCW_TAB_TITLE_MAX_CHARS 24
static GtkWidget *rcw_tab_title_label_new(const gchar *text)
{
	GtkWidget *label;
	glong len;

	if (!text)
		text = "";
	label = gtk_label_new(text);
	len = g_utf8_strlen(text, -1);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(GTK_LABEL(label), (gint)CLAMP(len, 4, RCW_TAB_TITLE_MAX_CHARS));
	gtk_label_set_max_width_chars(GTK_LABEL(label), RCW_TAB_TITLE_MAX_CHARS);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(label, text);
	return label;
}

static GtkWidget *rco_create_tab_label(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	GtkWidget *hbox;
	GtkWidget *widget;
	GtkWidget *button;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_show(hbox);

	widget = gtk_image_new_from_icon_name(remmina_file_get_icon_name(cnnobj->remmina_file), GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);

	widget = rcw_tab_title_label_new(remmina_file_get_string(cnnobj->remmina_file, "name"));

	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	button = gtk_button_new();      // The "x" to close the tab
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
#else
	gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
#endif
	gtk_widget_set_name(button, "remmina-small-button");
	gtk_widget_show(button);

	widget = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_container_add(GTK_CONTAINER(button), widget);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(rco_on_close_button_clicked), cnnobj);

	GtkWidget *event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
	gtk_container_add(GTK_CONTAINER(event_box), hbox);
	gtk_widget_show(event_box);

	return event_box;
}

static GtkWidget *rco_create_tab_page(RemminaConnectionObject *cnnobj)
{
	GtkWidget *page;

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(page, "remmina-tab-page");


	return page;
}

static GtkWidget *rcw_append_new_page(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	GtkWidget *page, *label;
	GtkNotebook *notebook;

	notebook = cnnwin->priv->notebook;

	page = rco_create_tab_page(cnnobj);
	g_object_set_data(G_OBJECT(page), "cnnobj", cnnobj);
	label = rco_create_tab_label(cnnobj);

	cnnobj->cnnwin = cnnwin;
	cnnobj->tab_page = page;

	gtk_notebook_append_page(notebook, page, label);
	gtk_notebook_set_tab_reorderable(notebook, page, TRUE);
	/* This trick prevents the tab label from being focused */
	gtk_widget_set_can_focus(gtk_widget_get_parent(label), FALSE);

	if (gtk_widget_get_parent(cnnobj->scrolled_container) != NULL)
		printf("REMMINA WARNING in %s: scrolled_container already has a parent\n", __func__);
	gtk_box_pack_start(GTK_BOX(page), cnnobj->scrolled_container, TRUE, TRUE, 0);

	GList *list = g_list_append(NULL, cnnobj);
	g_object_set_data(G_OBJECT(page), "cnnobj-list", list);
	g_object_set_data(G_OBJECT(page), "active-cnnobj", cnnobj);

	gtk_widget_show(page);

	return page;
}


static void rcw_update_notebook(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GtkNotebook *notebook;
	gint n;

	notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);

	/* The application window shows its tabs in the header bar strip, never in the notebook */
	if (cnnwin->priv->is_main) {
		gtk_notebook_set_show_tabs(notebook, FALSE);
		gtk_notebook_set_show_border(notebook, FALSE);
		return;
	}

	switch (cnnwin->priv->view_mode) {
	case SCROLLED_WINDOW_MODE:
		n = gtk_notebook_get_n_pages(notebook);
		gtk_notebook_set_show_tabs(notebook, remmina_pref.always_show_tab ? TRUE : n > 1);
		gtk_notebook_set_show_border(notebook, remmina_pref.always_show_tab ? TRUE : n > 1);
		break;
	default:
		gtk_notebook_set_show_tabs(notebook, FALSE);
		gtk_notebook_set_show_border(notebook, FALSE);
		break;
	}
}

static gboolean rcw_on_switch_page_finalsel(gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;

	if (!user_data)
		return FALSE;

	cnnobj = (RemminaConnectionObject *)user_data;
	if (!cnnobj->cnnwin)
		return FALSE;

	priv = cnnobj->cnnwin->priv;

	if (GTK_IS_WIDGET(cnnobj->cnnwin)) {
		rcw_floating_toolbar_show(cnnobj->cnnwin, TRUE);
		if (!priv->hidetb_eventsource)
			priv->hidetb_eventsource = g_timeout_add(TB_HIDE_TIME_TIME, (GSourceFunc)
								 rcw_floating_toolbar_hide, cnnobj->cnnwin);
		rco_update_toolbar(cnnobj);
		if (priv->is_main)
			rcw_update_empty_state(cnnobj->cnnwin);
		rcw_grab_focus(cnnobj->cnnwin);
		if (cnnobj->connected) {
			rcw_keyboard_grab_request(cnnobj, remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE));
		}
		if (priv->view_mode != SCROLLED_WINDOW_MODE)
			rco_check_resize(cnnobj);
	}
	priv->spf_eventsourceid = 0;
	return FALSE;
}

static void rcw_on_switch_page(GtkNotebook *notebook, GtkWidget *newpage, guint page_num,
			       RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj_newpage;

	cnnobj_newpage = g_object_get_data(G_OBJECT(newpage), "active-cnnobj");
	if (!cnnobj_newpage)
		cnnobj_newpage = g_object_get_data(G_OBJECT(newpage), "cnnobj");
	if (priv->is_main)
		rcw_tabstrip_sync_current(cnnwin, newpage);
	if (priv->spf_eventsourceid)
		g_source_remove(priv->spf_eventsourceid);
	if (!cnnobj_newpage) {
		/* A tool tab (editor, preferences…) became current */
		rcw_kp_ungrab(cnnwin);
		priv->spf_eventsourceid = g_idle_add(rcw_on_switch_tool_page_idle, cnnwin);
		return;
	}
	priv->spf_eventsourceid = g_idle_add(rcw_on_switch_page_finalsel, cnnobj_newpage);
}

static void rcw_on_page_added(GtkNotebook *notebook, GtkWidget *child, guint page_num,
			      RemminaConnectionWindow *cnnwin)
{
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(cnnwin->priv->notebook)) > 0)
		rcw_update_notebook(cnnwin);
	if (cnnwin->priv->is_main) {
		rcw_update_empty_state(cnnwin);
		rcw_tabstrip_rebuild(cnnwin);
	}
	rcw_session_changed(cnnwin);
}

static void rcw_on_page_reordered(GtkNotebook *notebook, GtkWidget *child, guint page_num,
				  RemminaConnectionWindow *cnnwin)
{
	if (cnnwin->priv && cnnwin->priv->is_main)
		rcw_tabstrip_rebuild(cnnwin);
	rcw_session_changed(cnnwin);
}

static gboolean rcw_session_tab_connect_idle(gpointer data);

/* The current tab is part of the session too; a restored tab connects when it is shown */
static void rcw_on_switch_page_session(GtkNotebook *notebook, GtkWidget *page, guint page_num,
				       RemminaConnectionWindow *cnnwin)
{
	rcw_session_changed(cnnwin);
	if (page && g_object_get_data(G_OBJECT(page), "session-tab"))
		g_idle_add(rcw_session_tab_connect_idle, g_object_ref(page));
}

static void rcw_on_page_removed(GtkNotebook *notebook, GtkWidget *child, guint page_num,
				RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (!priv)
		return;

	rcw_session_changed(cnnwin);
	if (priv->is_main)
		rcw_tabstrip_rebuild(cnnwin);
	if (priv->is_main && rcw_page_is_tab(child))
		rcw_close_if_empty_later(cnnwin);

	if (priv->is_main ? !rcw_has_connection_pages(cnnwin) : gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)) <= 0) {
		rcw_dispose_widget_areas(priv);
		if (priv->is_main && priv->closing_window) {
			/* Closing alone (other windows stay): its last tab is gone */
			if (priv->quit_watchdog_eventsource) {
				g_source_remove(priv->quit_watchdog_eventsource);
				priv->quit_watchdog_eventsource = 0;
			}
			rcw_close_all_connections(cnnwin);	/* tool tabs */
			gtk_widget_destroy(GTK_WIDGET(cnnwin));
			return;
		}
		if (priv->is_main) {
			/* The application window outlives its tabs */
			rcw_update_empty_state(cnnwin);
			if (priv->quit_requested) {
				if (priv->quit_watchdog_eventsource) {
					g_source_remove(priv->quit_watchdog_eventsource);
					priv->quit_watchdog_eventsource = 0;
				}
				g_idle_add(rcw_quit_idle, NULL);
			}
			return;
		}
		/* A fullscreen window emptied (tabs migrated back or all closed): show its application window again */
		if (priv->origin_win && priv->origin_win->priv && priv->origin_win->priv->fullscreen_win == cnnwin) {
			RemminaConnectionWindow *origin = priv->origin_win;
			origin->priv->fullscreen_win = NULL;
			if (!origin->priv->closing_window) {
				if (rcw_count_tabs(origin) == 0 && g_list_length(rcw_windows) > 1 && !rcw_app_quitting) {
					/* All its tabs were closed in fullscreen: it closes without showing again */
					rcw_close_if_empty_later(origin);
				} else {
					gtk_widget_show(GTK_WIDGET(origin));
					gtk_window_present(GTK_WINDOW(origin));
				}
			}
		}
		gtk_widget_destroy(GTK_WIDGET(cnnwin));
	} else {
		rcw_update_notebook(cnnwin);
		if (priv->is_main)
			rcw_update_empty_state(cnnwin);
	}
}

static GtkNotebook *
rcw_create_notebook(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GtkNotebook *notebook;

	notebook = GTK_NOTEBOOK(gtk_notebook_new());
	gtk_notebook_set_group_name(notebook, "remmina-rcw-group");

	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_widget_show(GTK_WIDGET(notebook));

	g_signal_connect(G_OBJECT(notebook), "switch-page", G_CALLBACK(rcw_on_switch_page), cnnwin);
	g_signal_connect(G_OBJECT(notebook), "page-added", G_CALLBACK(rcw_on_page_added), cnnwin);
	g_signal_connect(G_OBJECT(notebook), "page-removed", G_CALLBACK(rcw_on_page_removed), cnnwin);
	g_signal_connect(G_OBJECT(notebook), "page-reordered", G_CALLBACK(rcw_on_page_reordered), cnnwin);
	g_signal_connect_after(G_OBJECT(notebook), "switch-page", G_CALLBACK(rcw_on_switch_page_session), cnnwin);
	gtk_widget_set_can_focus(GTK_WIDGET(notebook), FALSE);

	return notebook;
}

/* The home page of a window that does not hold the connection list (it follows the focus) */
static GtkWidget *rcw_create_home_placeholder(void)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *w;

	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_vexpand(box, TRUE);
	w = gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(w), 64);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("The connection list is in the active window"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	gtk_widget_show_all(box);
	return box;
}

/* The placeholder shows while the connection list is elsewhere */
static void rcw_update_home_placeholder(RemminaConnectionWindow *cnnwin)
{
	GList *children, *l;
	gboolean holds_list = FALSE;

	if (!cnnwin->priv || !cnnwin->priv->sidebar_box || !cnnwin->priv->home_placeholder)
		return;
	children = gtk_container_get_children(GTK_CONTAINER(cnnwin->priv->sidebar_box));
	for (l = children; l; l = l->next)
		if (l->data != cnnwin->priv->home_placeholder)
			holds_list = TRUE;
	g_list_free(children);
	gtk_widget_set_visible(cnnwin->priv->home_placeholder, !holds_list);
}

/* The current window: new connections, tool tabs and dialogs go there; the list moves into it */
static void rcw_set_current(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindow *old = rcw_main;

	if (!cnnwin || !cnnwin->priv || !cnnwin->priv->is_main || cnnwin->priv->closing_window || cnnwin == rcw_main)
		return;
	rcw_main = cnnwin;
	remmina_main_attach_to_window(GTK_WINDOW(cnnwin));
	rcw_update_home_placeholder(cnnwin);
	if (old && old->priv)
		rcw_update_home_placeholder(old);
}

static void rcw_on_is_active(GObject *object, GParamSpec *pspec, gpointer data)
{
	if (gtk_window_is_active(GTK_WINDOW(object)))
		rcw_set_current(RCW(object));
}

/* ---- Sessions: what a window holds, kept in sessions.db ---- */

static GtkWidget *rcw_find_ftp_client(GtkWidget *widget)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (REMMINA_IS_FTP_CLIENT(widget))
		return widget;
	if (!GTK_IS_CONTAINER(widget))
		return NULL;
	children = gtk_container_get_children(GTK_CONTAINER(widget));
	for (l = children; l && !found; l = l->next)
		found = rcw_find_ftp_client(l->data);
	g_list_free(children);
	return found;
}

static RemminaSessionTab *rcw_session_tab_copy(const RemminaSessionTab *t)
{
	RemminaSessionTab *c = remmina_session_tab_new();

	c->profile = g_strdup(t->profile);
	c->name = g_strdup(t->name);
	c->protocol = g_strdup(t->protocol);
	c->server = g_strdup(t->server);
	c->username = g_strdup(t->username);
	c->sftp_left = g_strdup(t->sftp_left);
	c->sftp_right = g_strdup(t->sftp_right);
	return c;
}

/* A connection tab as a session keeps it: the profile, or a quick connection without its password */
static RemminaSessionTab *rcw_session_tab_of_page(GtkWidget *page)
{
	const RemminaSessionTab *waiting = g_object_get_data(G_OBJECT(page), "session-tab");
	RemminaConnectionObject *cnnobj = g_object_get_data(G_OBJECT(page), "cnnobj");
	RemminaSessionTab *tab;
	RemminaFile *file;
	const gchar *filename, *source;

	/* A restored tab not connected yet */
	if (waiting)
		return rcw_session_tab_copy(waiting);
	if (!cnnobj || !cnnobj->remmina_file)
		return NULL;
	file = cnnobj->remmina_file;
	tab = remmina_session_tab_new();
	filename = remmina_file_get_filename(file);
	source = remmina_file_get_string(file, "unidesk_source_profile");
	if (remmina_db_is_id(filename)) {
		tab->profile = g_strdup(filename);
	} else if (remmina_db_is_id(source) && remmina_db_profile_exists(source)) {
		/* Opened from a tab of that profile in the other protocol (SSH ↔ SFTP) */
		tab->profile = g_strdup(source);
		tab->protocol = g_strdup(remmina_file_get_string(file, "protocol"));
	} else {
		tab->protocol = g_strdup(remmina_file_get_string(file, "protocol"));
		tab->server = g_strdup(remmina_file_get_string(file, "server"));
		tab->username = g_strdup(remmina_file_get_string(file, "username"));
		if (!tab->protocol || !tab->server) {
			remmina_session_tab_free(tab);
			return NULL;
		}
	}
	tab->name = g_strdup(remmina_file_get_string(file, "name"));
	if (g_strcmp0(remmina_file_get_string(file, "protocol"), "SFTP") == 0 && cnnobj->proto) {
		GtkWidget *ftp = rcw_find_ftp_client(cnnobj->proto);
		if (ftp) {
			const gchar *left = NULL, *right = NULL;
			remmina_ftp_client_get_dirs(REMMINA_FTP_CLIENT(ftp), &left, &right);
			tab->sftp_left = g_strdup(left);
			tab->sftp_right = g_strdup(right);
		}
	}
	return tab;
}

/* The window as it is now into its session, written at once */
static void rcw_session_capture(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionWindow *holder;
	RemminaSession *s;
	GtkNotebook *notebook;
	GdkWindow *gdkwin;
	gint i, n, cur;

	if (!priv || !priv->is_main || !priv->session || priv->session_frozen)
		return;
	s = priv->session;
	/* In fullscreen the tabs are lent to the fullscreen window */
	holder = priv->fullscreen_win && priv->fullscreen_win->priv ? priv->fullscreen_win : cnnwin;
	notebook = holder->priv->notebook;
	g_ptr_array_set_size(s->tabs, 0);
	s->active = -1;
	if (notebook && GTK_IS_NOTEBOOK(notebook)) {
		cur = gtk_notebook_get_current_page(notebook);
		n = gtk_notebook_get_n_pages(notebook);
		for (i = 0; i < n; i++) {
			GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
			RemminaSessionTab *tab;
			/* Restored tabs not connected yet count, other tool tabs do not */
			if ((rcw_page_is_tool(page) && !g_object_get_data(G_OBJECT(page), "session-tab")) ||
			    !(tab = rcw_session_tab_of_page(page)))
				continue;
			if (i == cur)
				s->active = (gint)s->tabs->len;
			g_ptr_array_add(s->tabs, tab);
		}
	}
	gdkwin = gtk_widget_get_window(GTK_WIDGET(cnnwin));
	if (gdkwin && gtk_widget_get_visible(GTK_WIDGET(cnnwin))) {
		s->maximized = (gdk_window_get_state(gdkwin) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
		if (!s->maximized) {
			gtk_window_get_position(GTK_WINDOW(cnnwin), &s->x, &s->y);
			gtk_window_get_size(GTK_WINDOW(cnnwin), &s->width, &s->height);
		}
	}
	s->state = REMMINA_SESSION_OPEN;
	s->updated_at = g_get_real_time();
	if (s->tabs->len == 0 && !s->saved) {
		/* An empty window: nothing worth keeping (and no trace of windows never used) */
		if (priv->session_stored)
			remmina_session_delete(s->id);
		priv->session_stored = FALSE;
		return;
	}
	remmina_session_store(s);
	priv->session_stored = TRUE;
}

static gboolean rcw_session_save_cb(gpointer data)
{
	RemminaConnectionWindow *cnnwin = data;

	cnnwin->priv->session_save_id = 0;
	rcw_session_capture(cnnwin);
	return G_SOURCE_REMOVE;
}

/* Something the session keeps changed: write it a moment later (changes come in bursts) */
static void rcw_session_changed(RemminaConnectionWindow *cnnwin)
{
	if (!cnnwin || !cnnwin->priv)
		return;
	if (!cnnwin->priv->is_main)
		cnnwin = cnnwin->priv->origin_win;
	if (!cnnwin || !cnnwin->priv || !cnnwin->priv->session || cnnwin->priv->session_frozen ||
	    cnnwin->priv->session_save_id)
		return;
	cnnwin->priv->session_save_id = g_timeout_add(1000, rcw_session_save_cb, cnnwin);
}

/* The window closes (alone or at quit): its session as it is now, and no change after that */
static void rcw_session_freeze(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (!priv || !priv->is_main || !priv->session || priv->session_frozen)
		return;
	if (priv->session_save_id) {
		g_source_remove(priv->session_save_id);
		priv->session_save_id = 0;
	}
	rcw_session_capture(cnnwin);
	priv->session_frozen = TRUE;
}

static void rcw_session_freeze_all(void)
{
	GList *l;

	for (l = rcw_windows; l; l = l->next)
		rcw_session_freeze(l->data);
}

static gboolean rcw_on_configure_session(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	rcw_session_changed(RCW(widget));
	return FALSE;
}

/* ---- Restoring sessions ---- */

/* The smallest window size a session brings back: that of the preferences (remmina_pref.c) */
#define RCW_MIN_RESTORED_WIDTH	600
#define RCW_MIN_RESTORED_HEIGHT	400

/* "Continue": the sessions open at the last quit, taken once when the first window shows */
static GPtrArray *rcw_continue_set = NULL;
/* Meta+Shift+T: the tabs the user closed, the last one at the tail */
static GQueue rcw_closed_tabs = G_QUEUE_INIT;
#define RCW_CLOSED_TABS_KEPT 25
/* rcw_open_from_file_full() puts the connection in this window rather than in the current one */
static RemminaConnectionWindow *rcw_open_target = NULL;

/* What a session tab connects with: its profile, or a quick connection rebuilt (no password) */
static RemminaFile *rcw_session_tab_file(const RemminaSessionTab *tab)
{
	RemminaFile *file;

	if (tab->profile) {
		file = remmina_file_manager_load_file(tab->profile);
		/* The profile in the other protocol (SSH ↔ SFTP): a temporary copy, as when it was opened */
		if (file && tab->protocol && g_strcmp0(tab->protocol, remmina_file_get_string(file, "protocol")) != 0) {
			RemminaFile *other = remmina_file_dup_temp_protocol(file, tab->protocol);
			remmina_file_set_string(other, "unidesk_source_profile", tab->profile);
			remmina_file_free(file);
			file = other;
		}
		return file;
	}
	if (!tab->protocol || !tab->server)
		return NULL;
	file = remmina_file_new();
	remmina_file_set_string(file, "protocol", tab->protocol);
	remmina_file_set_string(file, "server", tab->server);
	remmina_file_set_string(file, "name", tab->name ? tab->name : tab->server);
	if (tab->username)
		remmina_file_set_string(file, "username", tab->username);
	remmina_file_set_int(file, "disablepasswordstoring", TRUE);
	return file;
}

static void rcw_session_page_on_connect_clicked(GtkButton *button, GtkWidget *page);

/* A restored tab not connected yet: a light page with its name; it connects when shown */
static GtkWidget *rcw_session_tab_page_new(const RemminaSessionTab *tab)
{
	RemminaFile *file = rcw_session_tab_file(tab);
	gboolean exists = file != NULL;
	GtkWidget *page, *w;
	gchar *title, *icon;

	if (file) {
		title = g_strdup(remmina_file_get_string(file, "name"));
		icon = g_strdup(remmina_file_get_icon_name(file));
		remmina_file_free(file);
	} else {
		title = g_strdup(tab->name ? tab->name : (tab->server ? tab->server : ""));
		icon = g_strdup("network-server-symbolic");
	}
	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign(page, GTK_ALIGN_FILL);
	w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
	gtk_widget_set_vexpand(w, TRUE);
	gtk_box_pack_start(GTK_BOX(page), w, TRUE, TRUE, 0);
	{
		GtkWidget *box = w, *img, *label, *button;
		img = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
		gtk_image_set_pixel_size(GTK_IMAGE(img), 64);
		gtk_style_context_add_class(gtk_widget_get_style_context(img), "dim-label");
		gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);
		label = gtk_label_new(title);
		gtk_style_context_add_class(gtk_widget_get_style_context(label), "title-2");
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
		label = gtk_label_new(exists ? _("Connects when the tab is shown") : _("This connection no longer exists"));
		gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
		g_object_set_data(G_OBJECT(page), "session-tab-status", label);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
		button = gtk_button_new_with_mnemonic(_("_Connect"));
		gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
		gtk_style_context_add_class(gtk_widget_get_style_context(button), "suggested-action");
		gtk_widget_set_sensitive(button, exists);
		g_signal_connect(button, "clicked", G_CALLBACK(rcw_session_page_on_connect_clicked), page);
		gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	}
	g_object_set_data_full(G_OBJECT(page), "session-tab", rcw_session_tab_copy(tab), (GDestroyNotify)remmina_session_tab_free);
	g_object_set_data_full(G_OBJECT(page), "tool-title", title, g_free);
	g_object_set_data_full(G_OBJECT(page), "tool-icon", icon, g_free);
	gtk_widget_show_all(page);
	return page;
}

static GtkWidget *rcw_append_session_tab(RemminaConnectionWindow *cnnwin, const RemminaSessionTab *tab)
{
	GtkWidget *page = rcw_session_tab_page_new(tab);
	GtkNotebook *notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	GtkWidget *label = rcw_tab_title_label_new(g_object_get_data(G_OBJECT(page), "tool-title"));

	gtk_widget_show(label);
	gtk_notebook_append_page(notebook, page, label);
	gtk_notebook_set_tab_reorderable(notebook, page, TRUE);
	return page;
}

/* The restored tab is shown: the real connection takes its place */
static void rcw_session_tab_connect(GtkWidget *page)
{
	RemminaSessionTab *tab = g_object_get_data(G_OBJECT(page), "session-tab");
	GtkWidget *top = gtk_widget_get_toplevel(page);
	RemminaConnectionWindow *cnnwin;
	GtkNotebook *notebook;
	RemminaFile *file;
	GtkWidget *proto, *newpage;
	gint pos;

	if (!tab || !REMMINA_IS_CONNECTION_WINDOW(top) || g_object_get_data(G_OBJECT(page), "session-tab-opening"))
		return;
	cnnwin = RCW(top);
	if (!cnnwin->priv || !cnnwin->priv->notebook)
		return;
	notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	pos = gtk_notebook_page_num(notebook, page);
	if (pos < 0 || !(file = rcw_session_tab_file(tab)))
		return;
	if (remmina_file_get_int(file, "profile-lock", FALSE) && remmina_unlock_new(GTK_WINDOW(cnnwin)) == 0) {
		remmina_file_free(file);
		return;
	}
	g_object_set_data(G_OBJECT(page), "session-tab-opening", GINT_TO_POINTER(1));
	rcw_open_target = cnnwin;
	proto = rcw_open_from_file_full(file, NULL, NULL, NULL);
	rcw_open_target = NULL;
	if (proto) {
		if (tab->sftp_left)
			g_object_set_data_full(G_OBJECT(proto), "unidesk-restore-left", g_strdup(tab->sftp_left), g_free);
		if (tab->sftp_right)
			g_object_set_data_full(G_OBJECT(proto), "unidesk-restore-right", g_strdup(tab->sftp_right), g_free);
		/* The connection came last: put it where the restored tab was */
		newpage = gtk_notebook_get_nth_page(notebook, gtk_notebook_get_n_pages(notebook) - 1);
		if (newpage && newpage != page)
			gtk_notebook_reorder_child(notebook, newpage, pos);
	}
	pos = gtk_notebook_page_num(notebook, page);
	if (pos >= 0)
		gtk_notebook_remove_page(notebook, pos);
}

static void rcw_session_page_on_connect_clicked(GtkButton *button, GtkWidget *page)
{
	rcw_session_tab_connect(page);
}

static gboolean rcw_session_tab_connect_idle(gpointer data)
{
	GtkWidget *page = data;
	GtkWidget *parent = gtk_widget_get_parent(page);

	/* Still the current tab of its window */
	if (parent && GTK_IS_NOTEBOOK(parent) &&
	    gtk_notebook_get_nth_page(GTK_NOTEBOOK(parent), gtk_notebook_get_current_page(GTK_NOTEBOOK(parent))) == page)
		rcw_session_tab_connect(page);
	g_object_unref(page);
	return G_SOURCE_REMOVE;
}

/* A window takes a restored session instead of the new one it was created with */
static void rcw_adopt_session(RemminaConnectionWindow *cnnwin, const RemminaSession *session)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->session_save_id) {
		g_source_remove(priv->session_save_id);
		priv->session_save_id = 0;
	}
	if (priv->session && priv->session_stored)
		remmina_session_delete(priv->session->id);
	remmina_session_free(priv->session);
	priv->session = remmina_session_copy(session);
	priv->session->state = REMMINA_SESSION_OPEN;
	priv->session_stored = FALSE;
	priv->session_frozen = FALSE;
	rcw_apply_accent(cnnwin);
}

/* Opens a session in a window: the current one if it holds no connection, else a new one */
static RemminaConnectionWindow *rcw_restore_session(const RemminaSession *session, RemminaConnectionWindow *into)
{
	RemminaConnectionWindow *cnnwin = into ? into : rcw_create_main();
	GtkNotebook *notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	GtkWidget *active = NULL;
	guint i;

	rcw_adopt_session(cnnwin, session);
	/* Not smaller than the window size kept in the preferences (remmina_pref.c): a size taken while
	 * the window manager squeezed the window (a tile among many) must not come back */
	if (session->width > 0 && session->height > 0)
		gtk_window_resize(GTK_WINDOW(cnnwin), MAX(RCW_MIN_RESTORED_WIDTH, session->width),
				  MAX(RCW_MIN_RESTORED_HEIGHT, session->height));
	if (session->x || session->y)
		gtk_window_move(GTK_WINDOW(cnnwin), session->x, session->y);
	if (session->maximized)
		gtk_window_maximize(GTK_WINDOW(cnnwin));
	for (i = 0; i < session->tabs->len; i++) {
		GtkWidget *page = rcw_append_session_tab(cnnwin, g_ptr_array_index(session->tabs, i));
		if ((gint)i == session->active)
			active = page;
	}
	gtk_widget_show(GTK_WIDGET(cnnwin));
	/* The tab that was current connects now; the others when they are shown */
	if (active)
		gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, active));
	rcw_session_changed(cnnwin);
	rcw_sessions_refresh_all();
	return cnnwin;
}

/* "Continue": every window of the last quit reopens with its tabs */
static void rcw_continue_sessions(void)
{
	RemminaConnectionWindow *first = NULL;
	guint i;

	if (!rcw_continue_set)
		return;
	for (i = 0; i < rcw_continue_set->len; i++) {
		const RemminaSession *s = g_ptr_array_index(rcw_continue_set, i);
		/* The first one into the window already there, if it holds no connection yet */
		RemminaConnectionWindow *into = (i == 0 && rcw_main && !rcw_has_connection_pages(rcw_main)) ? rcw_main : NULL;
		RemminaConnectionWindow *w = rcw_restore_session(s, into);
		if (!first)
			first = w;
	}
	g_clear_pointer(&rcw_continue_set, g_ptr_array_unref);
	if (first) {
		rcw_set_current(first);
		gtk_window_present(GTK_WINDOW(first));
	}
}

/* Meta+Shift+T: the tab closed last comes back in the current window */
static gboolean rcw_accel_reopen_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaSessionTab *tab;
	RemminaConnectionWindow *cnnwin;
	GtkWidget *page;

	if (rcw_keyboard_is_grabbed((RemminaConnectionWindow *)data))
		return FALSE;
	if (!(tab = g_queue_pop_tail(&rcw_closed_tabs)))
		return TRUE;
	cnnwin = rcw_get_main();
	page = rcw_append_session_tab(cnnwin, tab);
	remmina_session_tab_free(tab);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(cnnwin->priv->notebook),
				      gtk_notebook_page_num(GTK_NOTEBOOK(cnnwin->priv->notebook), page));
	return TRUE;
}

/* ---- Window accents: a colour per window (session) to tell similar windows apart ---- */

/* The names of data/style/unidesk-accent.css (GNOME accent palette), in the order they are offered */
static const struct {
	const gchar *	name;
	const gchar *	label;
} rcw_accents[] = {
	{ "orange", N_("Orange") },
	{ "yellow", N_("Yellow") },
	{ "teal",   N_("Teal")	 },
	{ "green",  N_("Green")	 },
	{ "red",    N_("Red")	 },
	{ "pink",   N_("Pink")	 },
	{ "blue",   N_("Blue")	 },
	{ "purple", N_("Purple") },
	{ "slate",  N_("Grey")	 },
};

static gboolean rcw_accent_is_known(const gchar *name)
{
	guint i;

	for (i = 0; name && i < G_N_ELEMENTS(rcw_accents); i++)
		if (g_strcmp0(rcw_accents[i].name, name) == 0)
			return TRUE;
	return FALSE;
}

/* A style sheet of data/style, once */
static void rcw_load_style(const gchar *file)
{
	static GHashTable *loaded = NULL;
	GtkCssProvider *provider;
	GBytes *css;
	gchar *path;

	if (!loaded)
		loaded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	if (g_hash_table_contains(loaded, file))
		return;
	g_hash_table_add(loaded, g_strdup(file));
	/* The bytes directly: gtk_css_provider_load_from_resource() folds "src/../data" (remmina_theme.c) */
	path = g_strconcat("/org/unidesk/Unidesk/src/../data/style/", file, NULL);
	css = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	g_free(path);
	if (!css)
		return;
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, g_bytes_get_data(css, NULL), (gssize)g_bytes_get_size(css), NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
						  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	g_bytes_unref(css);
}

static void rcw_load_accent_css(void)
{
	rcw_load_style("unidesk-accent.css");
}

/* The window shows the colour of its session */
static void rcw_apply_accent(RemminaConnectionWindow *cnnwin)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(cnnwin));
	const gchar *color = cnnwin->priv && cnnwin->priv->session ? cnnwin->priv->session->color : NULL;
	guint i;

	rcw_load_accent_css();
	for (i = 0; i < G_N_ELEMENTS(rcw_accents); i++) {
		gchar *klass = g_strconcat("unidesk-accent-", rcw_accents[i].name, NULL);
		if (g_strcmp0(color, rcw_accents[i].name) == 0)
			gtk_style_context_add_class(ctx, klass);
		else
			gtk_style_context_remove_class(ctx, klass);
		g_free(klass);
	}
}

static void rcw_set_accent(RemminaConnectionWindow *cnnwin, const gchar *color)
{
	if (!cnnwin || !cnnwin->priv || !cnnwin->priv->session)
		return;
	g_free(cnnwin->priv->session->color);
	cnnwin->priv->session->color = rcw_accent_is_known(color) ? g_strdup(color) : NULL;
	rcw_apply_accent(cnnwin);
	/* Written now: the lists of sessions read the file */
	if (cnnwin->priv->session_save_id) {
		g_source_remove(cnnwin->priv->session_save_id);
		cnnwin->priv->session_save_id = 0;
	}
	rcw_session_capture(cnnwin);
	rcw_sessions_refresh_all();
}

static void rcw_accent_on_swatch(GtkButton *button, gpointer data)
{
	RemminaConnectionWindow *cnnwin = g_object_get_data(G_OBJECT(button), "window");
	GtkWidget *popover = g_object_get_data(G_OBJECT(button), "popover");

	rcw_set_accent(cnnwin, (const gchar *)data);
	if (popover)
		gtk_popover_popdown(GTK_POPOVER(popover));
}

static GtkWidget *rcw_accent_swatch(GtkWidget *row, RemminaConnectionWindow *cnnwin, GtkWidget *popover,
				    const gchar *name, const gchar *label, gboolean selected)
{
	GtkWidget *button = gtk_button_new();
	GtkStyleContext *ctx = gtk_widget_get_style_context(button);
	gchar *klass = g_strconcat("unidesk-accent-swatch-", name ? name : "none", NULL);

	gtk_style_context_add_class(ctx, "unidesk-accent-swatch");
	gtk_style_context_add_class(ctx, klass);
	g_free(klass);
	if (selected) {
		gtk_style_context_add_class(ctx, "selected");
		gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("object-select-symbolic", GTK_ICON_SIZE_MENU));
	} else if (!name) {
		gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("action-unavailable-symbolic", GTK_ICON_SIZE_MENU));
	}
	gtk_widget_set_tooltip_text(button, label);
	g_object_set(gtk_widget_get_accessible(button), "accessible-name", label, NULL);
	g_object_set_data(G_OBJECT(button), "window", cnnwin);
	g_object_set_data(G_OBJECT(button), "popover", popover);
	g_signal_connect(button, "clicked", G_CALLBACK(rcw_accent_on_swatch), (gpointer)name);
	gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);
	return button;
}

static gboolean rcw_accent_popover_destroy_idle(gpointer popover)
{
	gtk_widget_destroy(GTK_WIDGET(popover));
	return G_SOURCE_REMOVE;
}

/* Not at once: the swatch that closed it is still handling its click */
static void rcw_accent_popover_on_closed(GtkPopover *popover, gpointer data)
{
	g_idle_add(rcw_accent_popover_destroy_idle, popover);
}

/* The colours, next to the widget that asked (the main menu button, the home tab) */
static void rcw_show_accent_popover(GtkWidget *relative_to, RemminaConnectionWindow *cnnwin)
{
	GtkWidget *popover = gtk_popover_new(relative_to);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *w;
	const gchar *color = cnnwin->priv->session ? cnnwin->priv->session->color : NULL;
	guint i;

	rcw_load_accent_css();
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	w = gtk_label_new(_("Window Colour"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "heading");
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("Marks the tabs of this window, to tell it from the others"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	gtk_widget_set_margin_top(row, 6);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	rcw_accent_swatch(row, cnnwin, popover, NULL, _("No Colour"), !rcw_accent_is_known(color));
	for (i = 0; i < G_N_ELEMENTS(rcw_accents); i++)
		rcw_accent_swatch(row, cnnwin, popover, rcw_accents[i].name, _(rcw_accents[i].label),
				  g_strcmp0(color, rcw_accents[i].name) == 0);
	gtk_widget_show_all(box);
	gtk_container_add(GTK_CONTAINER(popover), box);
	g_signal_connect(popover, "closed", G_CALLBACK(rcw_accent_popover_on_closed), NULL);
	gtk_popover_popup(GTK_POPOVER(popover));
}

static gboolean rcw_main_choose_accent_idle(gpointer data)
{
	RemminaConnectionWindow *cnnwin = rcw_get_main();
	GList *children = gtk_container_get_children(GTK_CONTAINER(cnnwin->priv->menu_slot));
	GtkWidget *anchor = children ? children->data : cnnwin->priv->menu_slot;

	g_list_free(children);
	rcw_show_accent_popover(anchor, cnnwin);
	return G_SOURCE_REMOVE;
}

/* Main menu "Window Colour…": the popover of the current window, under its main menu button
 * (once the menu has let the pointer go) */
void rcw_main_choose_accent(void)
{
	g_idle_add(rcw_main_choose_accent_idle, NULL);
}

/* A dot of the session's colour in the lists */
static GtkWidget *rcw_accent_dot(const gchar *color)
{
	GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gchar *klass;

	rcw_load_accent_css();
	gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
	if (rcw_accent_is_known(color)) {
		klass = g_strconcat("unidesk-accent-swatch-", color, NULL);
		gtk_style_context_add_class(gtk_widget_get_style_context(dot), "unidesk-accent-dot");
		gtk_style_context_add_class(gtk_widget_get_style_context(dot), klass);
		g_free(klass);
	} else {
		gtk_widget_set_size_request(dot, 10, 10);
	}
	return dot;
}

/* ---- Sessions in the lists: open, rename, save ---- */

static void rcw_history_fill(void);

static RemminaConnectionWindow *rcw_window_of_session(const gchar *id)
{
	GList *l;

	for (l = rcw_windows; l; l = l->next) {
		RemminaConnectionWindow *w = l->data;
		if (w->priv && w->priv->session && !w->priv->closing_window && g_strcmp0(w->priv->session->id, id) == 0)
			return w;
	}
	return NULL;
}

static gboolean rcw_sessions_refresh_idle(gpointer data)
{
	rcw_history_fill();
#ifdef HAVE_LIBAPPINDICATOR
	remmina_icon_populate_menu();
#endif
	return G_SOURCE_REMOVE;
}

/* The lists of sessions (history, tray) show the change; later: a button of a list may be what was
 * clicked */
static void rcw_sessions_refresh_all(void)
{
	g_idle_add(rcw_sessions_refresh_idle, NULL);
}

/* A session from a list: shown if a window holds it, else restored in a new window */
void rcw_open_session(const gchar *id)
{
	RemminaConnectionWindow *w = rcw_window_of_session(id);
	RemminaSession *s;

	if (w) {
		gtk_window_present(GTK_WINDOW(w->priv->fullscreen_win ? w->priv->fullscreen_win : w));
		return;
	}
	s = remmina_session_lookup(id);
	if (s) {
		w = rcw_restore_session(s, NULL);
		rcw_set_current(w);
		gtk_window_present(GTK_WINDOW(w));
		remmina_session_free(s);
	}
	rcw_sessions_refresh_all();
}

/* "Save": the session is kept until deleted */
static void rcw_session_save(const gchar *id)
{
	RemminaConnectionWindow *w = rcw_window_of_session(id);

	if (w)
		w->priv->session->saved = TRUE;
	remmina_session_set_saved(id, TRUE);
	if (w)
		rcw_session_changed(w);
	rcw_sessions_refresh_all();
}

/* "Rename…": a name from the user keeps the session too */
static void rcw_session_rename(GtkWindow *parent, const gchar *session_id)
{
	/* Its own copy: the list that asked may be refilled while the dialog runs */
	gchar *id = g_strdup(session_id);
	RemminaConnectionWindow *w = rcw_window_of_session(id);
	RemminaSession *s = w ? remmina_session_copy(w->priv->session) : remmina_session_lookup(id);
	GtkWidget *dialog, *entry, *content;

	if (!s) {
		g_free(id);
		return;
	}
	dialog = gtk_dialog_new_with_buttons(_("Rename Session"), parent,
					     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					     _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Rename"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK)),
				    "suggested-action");
	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), s->name ? s->name : "");
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 32);
	gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);
	gtk_widget_show_all(dialog);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		gchar *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
		if (name[0]) {
			w = rcw_window_of_session(id);
			if (w) {
				g_free(w->priv->session->name);
				w->priv->session->name = g_strdup(name);
				w->priv->session->saved = TRUE;
			}
			remmina_session_rename(id, name);
			if (w)
				rcw_session_changed(w);
		}
		g_free(name);
	}
	gtk_widget_destroy(dialog);
	remmina_session_free(s);
	g_free(id);
	rcw_sessions_refresh_all();
}

/* Main menu "Rename Session…": the session of the current window */
void rcw_main_rename_session(void)
{
	RemminaConnectionWindow *w = rcw_get_main();

	if (w->priv && w->priv->session)
		rcw_session_rename(GTK_WINDOW(w->priv->fullscreen_win ? w->priv->fullscreen_win : w), w->priv->session->id);
}

/* ---- History: the frequently used connections, then the sessions of closed windows by day ---- */

#define RCW_HISTORY_FREQUENT_MAX	6
#define RCW_HISTORY_TOAST_SECONDS	5

/* The right side of the home tab (one, it moves with the list to the current window) */
typedef struct {
	GtkWidget *	view;
	GtkWidget *	paned;		/* the home tab: the list on the left, the view on the right */
	GtkWidget *	list_side;
	GtkWidget *	toggle;		/* in a narrow window: shows the view in a popover */
	GtkWidget *	popover;
	gboolean	narrow;
	gboolean	position_set;	/* the divider was put where it was left */
	guint		position_save_id;
	GtkWidget *	box;		/* the groups, or the empty state */
	GtkWidget *	clear;		/* "Clear History" */
	GtkWidget *	toast;		/* revealer: what was removed, and "Undo" */
	GtkWidget *	toast_label;
	GtkWidget *	undo;
	guint		toast_id;
	gint		focus_index;	/* the row that takes the focus when the list is refilled, -1: none */
	gint		undo_index;	/* where the last removal took a row: the focus comes back there */
	GPtrArray *	undo_sessions;	/* RemminaSession: what the last removal deleted */
	GHashTable *	undo_counts;	/* profile id → its use count before the last removal */
} RcwHistory;

static RcwHistory *rcw_history = NULL;

typedef struct {
	gchar *	id;
	gint	count;
	gint64	last_used;
} RcwFrequent;

static void rcw_frequent_free(RcwFrequent *f)
{
	g_free(f->id);
	g_free(f);
}

static gint rcw_frequent_cmp(gconstpointer a, gconstpointer b)
{
	const RcwFrequent *fa = *(RcwFrequent *const *)a;
	const RcwFrequent *fb = *(RcwFrequent *const *)b;

	if (fa->count != fb->count)
		return fa->count > fb->count ? -1 : 1;
	return fa->last_used > fb->last_used ? -1 : (fa->last_used < fb->last_used ? 1 : 0);
}

/* Every connection made at least once: the most made first, the last used first among equals */
static GPtrArray *rcw_frequent_list(void)
{
	GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)rcw_frequent_free);
	gchar **ids = remmina_db_list_profiles(NULL);
	guint i;

	for (i = 0; ids[i]; i++) {
		gint count = remmina_db_use_count_get(ids[i]);
		RcwFrequent *f;
		if (count <= 0)
			continue;
		f = g_new0(RcwFrequent, 1);
		f->id = g_strdup(ids[i]);
		f->count = count;
		f->last_used = remmina_db_profile_get_last_used(ids[i]);
		g_ptr_array_add(list, f);
	}
	g_strfreev(ids);
	g_ptr_array_sort(list, rcw_frequent_cmp);
	return list;
}

/* When the session left its window */
static gint64 rcw_history_session_time(const RemminaSession *s)
{
	return s->closed_at ? s->closed_at : s->updated_at;
}

static gint rcw_history_session_cmp(gconstpointer a, gconstpointer b)
{
	gint64 ta = rcw_history_session_time(*(RemminaSession *const *)a);
	gint64 tb = rcw_history_session_time(*(RemminaSession *const *)b);

	return ta > tb ? -1 : (ta < tb ? 1 : 0);
}

/* The sessions no window holds, the newest first */
static GPtrArray *rcw_history_sessions(void)
{
	GPtrArray *all = remmina_session_list();
	GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_free);
	guint i;

	for (i = 0; i < all->len; i++) {
		RemminaSession *s = g_ptr_array_index(all, i);
		if (!rcw_window_of_session(s->id))
			g_ptr_array_add(list, remmina_session_copy(s));
	}
	g_ptr_array_unref(all);
	g_ptr_array_sort(list, rcw_history_session_cmp);
	return list;
}

/* ---- Removing, and undoing it (GNOME HIG: no question before, a toast to undo after) ---- */

/* A new removal: the one before stays done */
static void rcw_history_forget_undo(void)
{
	g_ptr_array_set_size(rcw_history->undo_sessions, 0);
	g_hash_table_remove_all(rcw_history->undo_counts);
}

static void rcw_history_hide_toast(void)
{
	if (rcw_history->toast_id) {
		g_source_remove(rcw_history->toast_id);
		rcw_history->toast_id = 0;
	}
	gtk_revealer_set_reveal_child(GTK_REVEALER(rcw_history->toast), FALSE);
}

static gboolean rcw_history_toast_timeout(gpointer data)
{
	rcw_history->toast_id = 0;
	rcw_history_hide_toast();
	rcw_history_forget_undo();
	return G_SOURCE_REMOVE;
}

static void rcw_history_toast(const gchar *text)
{
	gtk_label_set_text(GTK_LABEL(rcw_history->toast_label), text);
	if (rcw_history->toast_id)
		g_source_remove(rcw_history->toast_id);
	rcw_history->toast_id = g_timeout_add_seconds(RCW_HISTORY_TOAST_SECONDS, rcw_history_toast_timeout, NULL);
	gtk_revealer_set_reveal_child(GTK_REVEALER(rcw_history->toast), TRUE);
}

static void rcw_history_undo(void)
{
	guint i;

	if (!rcw_history || !gtk_revealer_get_reveal_child(GTK_REVEALER(rcw_history->toast)))
		return;
	for (i = 0; i < rcw_history->undo_sessions->len; i++)
		remmina_session_store(g_ptr_array_index(rcw_history->undo_sessions, i));
	remmina_db_use_counts_set(rcw_history->undo_counts);
	rcw_history_forget_undo();
	rcw_history_hide_toast();
	/* "Undo" goes with the toast: the focus returns to the row brought back */
	rcw_history->focus_index = rcw_history->undo_index;
	rcw_sessions_refresh_all();
}

static void rcw_history_on_undo(GtkButton *button, gpointer data)
{
	rcw_history_undo();
}

static void rcw_history_delete_session(const gchar *id, gint index)
{
	RemminaSession *s = remmina_session_lookup(id);

	/* Its window may have opened meanwhile */
	if (!s || rcw_window_of_session(id)) {
		remmina_session_free(s);
		return;
	}
	rcw_history_forget_undo();
	g_ptr_array_add(rcw_history->undo_sessions, s);
	remmina_session_delete(s->id);
	rcw_history->focus_index = rcw_history->undo_index = index;
	rcw_history_toast(_("Session deleted"));
	rcw_sessions_refresh_all();
}

/* Off the frequently used: the connection itself stays */
static void rcw_history_forget_connection(const gchar *id, gint index)
{
	gint count = remmina_db_use_count_get(id);

	if (count <= 0)
		return;
	rcw_history_forget_undo();
	g_hash_table_insert(rcw_history->undo_counts, g_strdup(id), GINT_TO_POINTER(count));
	remmina_db_use_count_set(id, 0);
	rcw_history->focus_index = rcw_history->undo_index = index;
	rcw_history_toast(_("Connection removed from frequently used"));
	rcw_sessions_refresh_all();
}

/* The trash of a row, or Delete on it */
static void rcw_history_remove_row(GtkWidget *row)
{
	const gchar *session = g_object_get_data(G_OBJECT(row), "session-id");
	const gchar *profile = g_object_get_data(G_OBJECT(row), "profile-id");
	gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "history-index"));

	if (session)
		rcw_history_delete_session(session, index);
	else if (profile)
		rcw_history_forget_connection(profile, index);
}

/* "Clear History": every session it shows (saved ones too) and every use count; the windows, their
 * sessions and the connections themselves stay */
static void rcw_history_on_clear(GtkButton *button, gpointer data)
{
	GPtrArray *sessions = rcw_history_sessions();
	GPtrArray *frequent = rcw_frequent_list();
	GHashTable *zero = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;

	rcw_history_forget_undo();
	for (i = 0; i < sessions->len; i++) {
		RemminaSession *s = g_ptr_array_index(sessions, i);
		remmina_session_delete(s->id);
		g_ptr_array_add(rcw_history->undo_sessions, remmina_session_copy(s));
	}
	for (i = 0; i < frequent->len; i++) {
		RcwFrequent *f = g_ptr_array_index(frequent, i);
		g_hash_table_insert(rcw_history->undo_counts, g_strdup(f->id), GINT_TO_POINTER(f->count));
		g_hash_table_insert(zero, f->id, GINT_TO_POINTER(0));
	}
	remmina_db_use_counts_set(zero);
	g_hash_table_unref(zero);
	g_ptr_array_unref(frequent);
	g_ptr_array_unref(sessions);
	rcw_history->focus_index = -1;
	rcw_history->undo_index = 0;
	rcw_history_toast(_("History cleared"));
	/* The broom is insensitive now: the focus (and Ctrl+Z) stays in History, on "Undo" */
	gtk_widget_grab_focus(rcw_history->undo);
	rcw_sessions_refresh_all();
}

/* ---- Opening ---- */

/* As from the list of connections: a new tab of the current window */
static void rcw_history_connect(const gchar *id)
{
	if (remmina_pref_get_boolean("use_primary_password") && remmina_pref_get_boolean("lock_connect") &&
	    remmina_unlock_new(GTK_WINDOW(rcw_get_main())) == 0)
		return;
	remmina_db_profile_touch(id);
	rcw_open_from_filename(id);
}

static gboolean rcw_history_popdown_idle(gpointer data)
{
	if (rcw_history && rcw_history->toggle)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rcw_history->toggle), FALSE);
	return G_SOURCE_REMOVE;
}

/* A row: its session in a new window, or its connection in a new tab (copies: the list is refilled
 * meanwhile); the popover of a narrow window goes, once the click is over */
static void rcw_history_open_row(GtkWidget *row)
{
	gchar *session = g_strdup(g_object_get_data(G_OBJECT(row), "session-id"));
	gchar *profile = g_strdup(g_object_get_data(G_OBJECT(row), "profile-id"));

	g_idle_add(rcw_history_popdown_idle, NULL);
	if (session)
		rcw_open_session(session);
	else if (profile)
		rcw_history_connect(profile);
	g_free(session);
	g_free(profile);
}

static void rcw_history_on_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
	rcw_history_open_row(GTK_WIDGET(row));
}

/* ---- The menu of a session: open, save, rename, delete ---- */

static gboolean rcw_menu_destroy_idle(gpointer menu)
{
	gtk_widget_destroy(GTK_WIDGET(menu));
	g_object_unref(menu);
	return G_SOURCE_REMOVE;
}

/* A popup menu goes once used, but not at once: GTK deactivates the menu before it activates the
 * chosen item, and an item destroyed by then has lost its handlers */
static void rcw_menu_on_deactivate(GtkMenuShell *menu, gpointer data)
{
	g_idle_add(rcw_menu_destroy_idle, g_object_ref(menu));
}

static void rcw_history_menu_on_open(GtkMenuItem *item, gpointer data)
{
	gchar *id = g_strdup(g_object_get_data(G_OBJECT(item), "session-id"));

	g_idle_add(rcw_history_popdown_idle, NULL);
	rcw_open_session(id);
	g_free(id);
}

static void rcw_history_menu_on_save(GtkMenuItem *item, gpointer data)
{
	rcw_session_save(g_object_get_data(G_OBJECT(item), "session-id"));
}

static void rcw_history_menu_on_rename(GtkMenuItem *item, gpointer data)
{
	if (rcw_history)
		rcw_session_rename(GTK_WINDOW(gtk_widget_get_toplevel(rcw_history->view)), g_object_get_data(G_OBJECT(item), "session-id"));
}

static void rcw_history_menu_on_delete(GtkMenuItem *item, gpointer data)
{
	if (rcw_history)
		rcw_history_delete_session(g_object_get_data(G_OBJECT(item), "session-id"),
					   GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "history-index")));
}

/* The items keep the session's id: the row may be gone when one is chosen */
static void rcw_history_menu_item(GtkWidget *menu, const gchar *label, GtkWidget *row, GCallback cb)
{
	GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);

	g_object_set_data_full(G_OBJECT(item), "session-id", g_strdup(g_object_get_data(G_OBJECT(row), "session-id")), g_free);
	g_object_set_data(G_OBJECT(item), "history-index", g_object_get_data(G_OBJECT(row), "history-index"));
	g_signal_connect(item, "activate", cb, NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* Right click, the Menu key or Shift+F10 on a session (a connection has only its trash) */
static void rcw_history_popup_menu(GtkWidget *row, const GdkEvent *event)
{
	RemminaSession *s = remmina_session_lookup(g_object_get_data(G_OBJECT(row), "session-id"));
	GtkWidget *menu;

	if (!s)
		return;
	menu = gtk_menu_new();
	rcw_history_menu_item(menu, _("_Open"), row, G_CALLBACK(rcw_history_menu_on_open));
	if (!s->saved)
		rcw_history_menu_item(menu, _("_Save"), row, G_CALLBACK(rcw_history_menu_on_save));
	rcw_history_menu_item(menu, _("_Rename…"), row, G_CALLBACK(rcw_history_menu_on_rename));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	rcw_history_menu_item(menu, _("_Delete"), row, G_CALLBACK(rcw_history_menu_on_delete));
	remmina_session_free(s);
	gtk_widget_show_all(menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), rcw_history->view, NULL);
	g_signal_connect(menu, "deactivate", G_CALLBACK(rcw_menu_on_deactivate), NULL);
	if (event)
		gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
	else
		gtk_menu_popup_at_widget(GTK_MENU(menu), row, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static gboolean rcw_history_on_list_button_press(GtkWidget *list, GdkEventButton *event, gpointer data)
{
	GtkWidget *target, *row;

	if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
		return FALSE;
	/* On the trash the event comes in its coordinates: the row is found from the widget */
	target = gtk_get_event_widget((GdkEvent *)event);
	row = target ? gtk_widget_get_ancestor(target, GTK_TYPE_LIST_BOX_ROW) : NULL;
	if (!row)
		row = GTK_WIDGET(gtk_list_box_get_row_at_y(GTK_LIST_BOX(list), (gint)event->y));
	if (!row || !g_object_get_data(G_OBJECT(row), "session-id"))
		return FALSE;
	gtk_widget_grab_focus(row);
	rcw_history_popup_menu(row, (GdkEvent *)event);
	return TRUE;
}

/* ---- Keyboard ---- */

static GtkWidget *rcw_history_focus_row(void)
{
	GtkWidget *top = gtk_widget_get_toplevel(rcw_history->view);
	GtkWidget *focus = GTK_IS_WINDOW(top) ? gtk_window_get_focus(GTK_WINDOW(top)) : NULL;

	if (!focus || !gtk_widget_is_ancestor(focus, rcw_history->view))
		return NULL;
	return gtk_widget_get_ancestor(focus, GTK_TYPE_LIST_BOX_ROW);
}

/* Delete removes the row with the focus, the Menu key opens its menu, Ctrl+Z undoes (Enter opens:
 * the lists do it) */
static gboolean rcw_history_on_key_press(GtkWidget *dialog, GdkEventKey *event, gpointer data)
{
	GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
	guint keyval = event->keyval;
	GtkWidget *row;

	/* Ctrl+Z on the key of Z whatever the layout, as accelerators do */
	gdk_keymap_translate_keyboard_state(gdk_keymap_get_for_display(gtk_widget_get_display(dialog)), event->hardware_keycode,
					    (GdkModifierType)(event->state & ~GDK_SHIFT_MASK), 0, &keyval, NULL, NULL, NULL);
	if (mods == GDK_CONTROL_MASK && gdk_keyval_to_lower(keyval) == GDK_KEY_z) {
		rcw_history_undo();
		return TRUE;
	}
	row = rcw_history_focus_row();
	if (!row)
		return FALSE;
	if ((event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) && mods == 0) {
		rcw_history_remove_row(row);
		return TRUE;
	}
	if ((event->keyval == GDK_KEY_Menu && mods == 0) || (event->keyval == GDK_KEY_F10 && mods == GDK_SHIFT_MASK)) {
		rcw_history_popup_menu(row, NULL);
		return TRUE;
	}
	return FALSE;
}

/* The arrows go on from one group to the next */
static gboolean rcw_history_on_keynav_failed(GtkWidget *list, GtkDirectionType direction, gpointer data)
{
	GList *children, *l, *rows, *r;
	GtkWidget *next = NULL;
	gboolean down = direction == GTK_DIR_DOWN;

	if (direction != GTK_DIR_UP && direction != GTK_DIR_DOWN)
		return FALSE;
	children = gtk_container_get_children(GTK_CONTAINER(rcw_history->box));
	for (l = g_list_find(children, list), l = l ? (down ? l->next : l->prev) : NULL; l && !next; l = down ? l->next : l->prev)
		if (GTK_IS_LIST_BOX(l->data))
			next = l->data;
	g_list_free(children);
	if (!next)
		return FALSE;
	rows = gtk_container_get_children(GTK_CONTAINER(next));
	r = down ? rows : g_list_last(rows);
	if (r)
		gtk_widget_grab_focus(r->data);
	g_list_free(rows);
	return TRUE;
}

/* ---- The rows ---- */

/* Its sign (a protocol, a colour), its name, a line under it, and the trash */
static GtkWidget *rcw_history_row(GtkWidget *lead, const gchar *title, const gchar *details, const gchar *remove_label)
{
	GtkWidget *row = gtk_list_box_row_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *sign = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *w;

	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 6);
	gtk_widget_set_margin_top(box, 6);
	gtk_widget_set_margin_bottom(box, 6);
	/* As wide as an icon, so the names of all the groups line up */
	gtk_widget_set_size_request(sign, 16, -1);
	gtk_widget_set_halign(lead, GTK_ALIGN_CENTER);
	gtk_box_set_center_widget(GTK_BOX(sign), lead);
	gtk_box_pack_start(GTK_BOX(box), sign, FALSE, FALSE, 0);
	gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
	w = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(text), w, FALSE, FALSE, 0);
	if (details && details[0]) {
		w = gtk_label_new(details);
		gtk_label_set_xalign(GTK_LABEL(w), 0);
		gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
		gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
		gtk_box_pack_start(GTK_BOX(text), w, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
	w = gtk_button_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "flat");
	gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(w, remove_label);
	g_object_set(gtk_widget_get_accessible(w), "accessible-name", remove_label, NULL);
	g_signal_connect_swapped(w, "clicked", G_CALLBACK(rcw_history_remove_row), row);
	gtk_box_pack_end(GTK_BOX(box), w, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(row), box);
	g_object_set(gtk_widget_get_accessible(row), "accessible-name", title ? title : "", NULL);
	return row;
}

/* A group: its title, then its rows in one rounded block */
static GtkWidget *rcw_history_group(GtkWidget *box, const gchar *title)
{
	GtkWidget *label = gtk_label_new(title), *list = gtk_list_box_new();
	GList *children = gtk_container_get_children(GTK_CONTAINER(box));

	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "heading");
	/* 6 from the title to its rows (the box), 18 between the groups */
	if (children)
		gtk_widget_set_margin_top(label, 12);
	g_list_free(children);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(list), "unidesk-boxed-list");
	g_object_set(gtk_widget_get_accessible(list), "accessible-name", title, NULL);
	g_signal_connect(list, "row-activated", G_CALLBACK(rcw_history_on_row_activated), NULL);
	g_signal_connect(list, "button-press-event", G_CALLBACK(rcw_history_on_list_button_press), NULL);
	g_signal_connect(list, "keynav-failed", G_CALLBACK(rcw_history_on_keynav_failed), NULL);
	gtk_box_pack_start(GTK_BOX(box), list, FALSE, FALSE, 0);
	return list;
}

static void rcw_history_add_row(GtkWidget *list, GtkWidget *row, gint index, GtkWidget **focus, GtkWidget **last)
{
	g_object_set_data(G_OBJECT(row), "history-index", GINT_TO_POINTER(index));
	gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
	if (index <= rcw_history->focus_index)
		*focus = row;
	*last = row;
}

static void rcw_history_add_empty(GtkWidget *box)
{
	GtkWidget *empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12), *w;

	gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
	gtk_widget_set_vexpand(empty, TRUE);
	w = gtk_image_new_from_icon_name("document-open-recent-symbolic", GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(w), 96);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(empty), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("History Is Empty"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "title-2");
	gtk_box_pack_start(GTK_BOX(empty), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("Frequently used connections and the sessions of closed windows appear here"));
	gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_CENTER);
	gtk_label_set_max_width_chars(GTK_LABEL(w), 40);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(empty), w, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), empty, TRUE, TRUE, 0);
}

static void rcw_history_fill(void)
{
	GtkWidget *box, *list = NULL, *row, *focus = NULL, *last = NULL;
	GPtrArray *frequent, *sessions;
	GList *children, *l;
	gchar *day = NULL;
	gint index = 0;
	guint i;

	if (!rcw_history)
		return;
	box = rcw_history->box;
	/* The focus stays on the same place after a refill */
	if (rcw_history->focus_index < 0 && (row = rcw_history_focus_row()) != NULL)
		rcw_history->focus_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "history-index"));
	children = gtk_container_get_children(GTK_CONTAINER(box));
	for (l = children; l; l = l->next)
		gtk_widget_destroy(l->data);
	g_list_free(children);

	frequent = rcw_frequent_list();
	for (i = 0; i < frequent->len && index < RCW_HISTORY_FREQUENT_MAX; i++) {
		RcwFrequent *f = g_ptr_array_index(frequent, i);
		RemminaFile *file = remmina_file_manager_load_file(f->id);
		const gchar *name, *server;
		if (!file)
			continue;
		if (!list)
			list = rcw_history_group(box, _("Frequently Used"));
		name = remmina_file_get_string(file, "name");
		server = remmina_file_get_string(file, "server");
		if (!server || !server[0])
			server = remmina_file_get_string(file, "protocol");
		row = rcw_history_row(gtk_image_new_from_icon_name(remmina_file_get_icon_name(file), GTK_ICON_SIZE_BUTTON),
				      name ? name : server, server, _("Remove from Frequently Used"));
		g_object_set_data_full(G_OBJECT(row), "profile-id", g_strdup(f->id), g_free);
		rcw_history_add_row(list, row, index++, &focus, &last);
		remmina_file_free(file);
	}
	g_ptr_array_unref(frequent);

	/* A group per day, the newest first */
	sessions = rcw_history_sessions();
	for (i = 0; i < sessions->len; i++) {
		RemminaSession *s = g_ptr_array_index(sessions, i);
		GDateTime *dt = g_date_time_new_from_unix_local(rcw_history_session_time(s) / G_USEC_PER_SEC);
		gchar *date = g_date_time_format(dt, "%d.%m.%Y");
		gchar *time = g_date_time_format(dt, "%H:%M");
		gchar *tabs = g_strdup_printf(ngettext("%u tab", "%u tabs", s->tabs->len), s->tabs->len);
		/* Under the name: "3 tabs · 18:30", and "saved" for a session the user keeps */
		gchar *details = s->saved ? g_strdup_printf("%s · %s · %s", tabs, time, C_("session", "saved"))
				 : g_strdup_printf("%s · %s", tabs, time);
		if (g_strcmp0(date, day) != 0) {
			list = rcw_history_group(box, date);
			g_free(day);
			day = g_strdup(date);
		}
		row = rcw_history_row(rcw_accent_dot(s->color), s->name, details, _("Delete"));
		g_object_set_data_full(G_OBJECT(row), "session-id", g_strdup(s->id), g_free);
		rcw_history_add_row(list, row, index++, &focus, &last);
		g_free(details);
		g_free(tabs);
		g_free(time);
		g_free(date);
		g_date_time_unref(dt);
	}
	g_free(day);
	g_ptr_array_unref(sessions);

	if (index == 0)
		rcw_history_add_empty(box);
	gtk_widget_set_sensitive(rcw_history->clear, index > 0);
	gtk_widget_show_all(box);
	if (rcw_history->focus_index >= 0) {
		/* The row now in that place, or the last one */
		if (focus || last)
			gtk_widget_grab_focus(focus ? focus : last);
		rcw_history->focus_index = -1;
	}
}

static void rcw_history_on_destroy(GtkWidget *view, gpointer data)
{
	RcwHistory *h = rcw_history;

	rcw_history = NULL;
	if (h->toast_id)
		g_source_remove(h->toast_id);
	if (h->position_save_id)
		g_source_remove(h->position_save_id);
	g_ptr_array_unref(h->undo_sessions);
	g_hash_table_unref(h->undo_counts);
	g_free(h);
}

/* The use counts change as connections are made: fresh whenever it shows */
static void rcw_history_on_map(GtkWidget *view, gpointer data)
{
	rcw_history_fill();
}

/* History: a title with "Clear History", the frequently used connections, then the sessions of the
 * closed windows by day; a toast at the bottom undoes a removal */
static GtkWidget *rcw_history_view_new(void)
{
	GtkWidget *view, *head, *overlay, *scrolled, *box, *frame, *hbox, *w;

	rcw_load_accent_css();
	rcw_load_style("unidesk-history.css");
	rcw_history = g_new0(RcwHistory, 1);
	rcw_history->focus_index = -1;
	rcw_history->undo_sessions = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_free);
	rcw_history->undo_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	rcw_history->view = view;
	g_signal_connect(view, "destroy", G_CALLBACK(rcw_history_on_destroy), NULL);
	g_signal_connect(view, "key-press-event", G_CALLBACK(rcw_history_on_key_press), NULL);
	g_signal_connect(view, "map", G_CALLBACK(rcw_history_on_map), NULL);

	/* The title row, as high as the row of buttons over the list */
	head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(head, 6);
	gtk_widget_set_margin_end(head, 6);
	gtk_widget_set_margin_top(head, 6);
	gtk_widget_set_margin_bottom(head, 6);
	w = gtk_label_new(_("History"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "heading");
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_widget_set_margin_start(w, 6);
	gtk_box_pack_start(GTK_BOX(head), w, TRUE, TRUE, 0);
	w = gtk_button_new_from_icon_name("edit-clear-all-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "flat");
	gtk_widget_set_tooltip_text(w, _("Clear History"));
	g_object_set(gtk_widget_get_accessible(w), "accessible-name", _("Clear History"), NULL);
	g_signal_connect(w, "clicked", G_CALLBACK(rcw_history_on_clear), NULL);
	gtk_box_pack_end(GTK_BOX(head), w, FALSE, FALSE, 0);
	rcw_history->clear = w;
	gtk_box_pack_start(GTK_BOX(view), head, FALSE, FALSE, 0);

	overlay = gtk_overlay_new();
	gtk_box_pack_start(GTK_BOX(view), overlay, TRUE, TRUE, 0);
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_container_add(GTK_CONTAINER(overlay), scrolled);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 6);
	gtk_widget_set_margin_bottom(box, 18);
	gtk_container_add(GTK_CONTAINER(scrolled), box);
	rcw_history->box = box;

	/* The toast at the bottom: what was removed, and "Undo" */
	rcw_history->toast = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(rcw_history->toast), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
	gtk_widget_set_halign(rcw_history->toast, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(rcw_history->toast, GTK_ALIGN_END);
	frame = gtk_frame_new(NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(frame), "app-notification");
	gtk_style_context_add_class(gtk_widget_get_style_context(frame), "unidesk-toast");
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	rcw_history->toast_label = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(hbox), rcw_history->toast_label, FALSE, FALSE, 0);
	w = gtk_button_new_with_mnemonic(_("_Undo"));
	rcw_history->undo = w;
	gtk_widget_set_tooltip_text(w, _("Undo (Ctrl+Z)"));
	g_signal_connect(w, "clicked", G_CALLBACK(rcw_history_on_undo), NULL);
	gtk_box_pack_end(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_container_add(GTK_CONTAINER(rcw_history->toast), frame);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), rcw_history->toast);
	gtk_widget_show_all(view);
	/* Shown or hidden by the home tab only (a narrow window hides it) */
	gtk_widget_set_no_show_all(view, TRUE);
	rcw_history_fill();
	return view;
}

/* ---- The home tab: the list on the left, History on the right ---- */

#define RCW_HOME_NARROW_WIDTH	700		/* narrower, History hides behind a button */
#define RCW_HOME_HISTORY_MIN	280		/* History is never narrower on the right */
#define RCW_HOME_SPLIT_KEY	"home_split"	/* the divider, a share of the width (unidesk.pref) */

static gdouble rcw_home_split_share(void)
{
	gchar *value = remmina_pref_get_value(RCW_HOME_SPLIT_KEY);
	gdouble share = value ? g_ascii_strtod(value, NULL) : 0;

	g_free(value);
	return share >= 0.2 && share <= 0.8 ? share : 0.5;
}

static gboolean rcw_home_split_save(gpointer data)
{
	gint width = gtk_widget_get_allocated_width(rcw_history->paned);
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	rcw_history->position_save_id = 0;
	if (width > 0 && !rcw_history->narrow)
		remmina_pref_set_value(RCW_HOME_SPLIT_KEY,
				       g_ascii_formatd(buf, sizeof(buf), "%.3f", (gdouble)gtk_paned_get_position(GTK_PANED(rcw_history->paned)) / width));
	return G_SOURCE_REMOVE;
}

/* Where the divider may go: the list keeps its smallest width, History RCW_HOME_HISTORY_MIN */
static gint rcw_home_split_clamp(gint position, gint width)
{
	gint list_min = 0;

	gtk_widget_get_preferred_width(rcw_history->list_side, &list_min, NULL);
	return MAX(list_min, MIN(position, width - RCW_HOME_HISTORY_MIN));
}

/* The divider moved (by the user, or with the window: the share stays): kept once it rests */
static void rcw_home_split_on_position(GObject *paned, GParamSpec *pspec, gpointer data)
{
	gint width, position, clamped;

	if (!rcw_history || !rcw_history->position_set || rcw_history->narrow)
		return;
	width = gtk_widget_get_allocated_width(rcw_history->paned);
	position = gtk_paned_get_position(GTK_PANED(rcw_history->paned));
	/* History may shrink (so the window may narrow), but not by the divider */
	clamped = rcw_home_split_clamp(position, width);
	if (width > 0 && clamped != position) {
		gtk_paned_set_position(GTK_PANED(rcw_history->paned), clamped);
		return;
	}
	if (rcw_history->position_save_id)
		g_source_remove(rcw_history->position_save_id);
	rcw_history->position_save_id = g_timeout_add(500, rcw_home_split_save, NULL);
}

/* History back on the right of the home tab (from the popover of a narrow window) */
static void rcw_home_split_view_home(void)
{
	GtkWidget *view = rcw_history->view, *parent = gtk_widget_get_parent(view);

	if (parent != rcw_history->paned) {
		g_object_ref(view);
		if (parent)
			gtk_container_remove(GTK_CONTAINER(parent), view);
		gtk_paned_pack2(GTK_PANED(rcw_history->paned), view, TRUE, TRUE);
		g_object_unref(view);
	}
	gtk_widget_set_size_request(view, -1, -1);
	gtk_widget_set_visible(view, !rcw_history->narrow);
}

static gboolean rcw_home_split_apply_idle(gpointer data)
{
	if (!rcw_history)
		return G_SOURCE_REMOVE;
	if (!rcw_history->narrow)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rcw_history->toggle), FALSE);
	gtk_widget_set_visible(rcw_history->toggle, rcw_history->narrow);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rcw_history->toggle)))
		rcw_home_split_view_home();
	return G_SOURCE_REMOVE;
}

/* The divider at its share of the width, once History shows beside the list at this width (set
 * earlier, GTK would rescale it from the width before) */
static gboolean rcw_home_split_share_idle(gpointer data)
{
	gint width;

	if (!rcw_history)
		return G_SOURCE_REMOVE;
	width = gtk_widget_get_allocated_width(rcw_history->paned);
	if (!rcw_history->narrow && width > 0)
		gtk_paned_set_position(GTK_PANED(rcw_history->paned), rcw_home_split_clamp((gint)(width * rcw_home_split_share()), width));
	return G_SOURCE_REMOVE;
}

static void rcw_home_split_on_size_allocate(GtkWidget *paned, GdkRectangle *allocation, gpointer data)
{
	gint list_min = 0;
	gboolean narrow;

	if (!rcw_history)
		return;
	/* Narrow: no room for History beside the list as it needs to be */
	gtk_widget_get_preferred_width(rcw_history->list_side, &list_min, NULL);
	narrow = allocation->width < MAX(RCW_HOME_NARROW_WIDTH, list_min + RCW_HOME_HISTORY_MIN + 6);
	if (narrow != rcw_history->narrow) {
		rcw_history->narrow = narrow;
		/* Wide again: the divider goes back to its share */
		rcw_history->position_set = FALSE;
		/* Not while GTK allocates */
		g_idle_add(rcw_home_split_apply_idle, NULL);
		return;
	}
	if (!narrow && !rcw_history->position_set && gtk_widget_get_visible(rcw_history->view) &&
	    gtk_widget_get_parent(rcw_history->view) == paned) {
		rcw_history->position_set = TRUE;
		g_idle_add(rcw_home_split_share_idle, NULL);
	}
}

static void rcw_home_split_on_popover_closed(GtkPopover *popover, gpointer data)
{
	if (rcw_history)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rcw_history->toggle), FALSE);
}

/* A narrow window: History in a popover under its button */
static void rcw_home_split_on_toggle(GtkToggleButton *toggle, gpointer data)
{
	GtkWidget *view;

	if (!rcw_history)
		return;
	view = rcw_history->view;
	if (!gtk_toggle_button_get_active(toggle)) {
		if (rcw_history->popover && gtk_widget_get_visible(rcw_history->popover))
			gtk_popover_popdown(GTK_POPOVER(rcw_history->popover));
		rcw_home_split_view_home();
		return;
	}
	if (!rcw_history->popover) {
		rcw_history->popover = gtk_popover_new(GTK_WIDGET(toggle));
		g_signal_connect(rcw_history->popover, "closed", G_CALLBACK(rcw_home_split_on_popover_closed), NULL);
	}
	g_object_ref(view);
	gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(view)), view);
	gtk_container_add(GTK_CONTAINER(rcw_history->popover), view);
	g_object_unref(view);
	gtk_widget_set_size_request(view, 380, 480);
	gtk_widget_show(view);
	gtk_popover_popup(GTK_POPOVER(rcw_history->popover));
}

/* The home tab (one, it moves with the list): the list side on the left, History on the right, a
 * divider between them that stays where it was left; in a narrow window History hides, and a button
 * at the end of the list's row of buttons shows it in a popover */
GtkWidget *rcw_home_split_new(GtkWidget *list_side, GtkWidget *tools_row)
{
	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	GtkWidget *view = rcw_history_view_new();
	GtkWidget *toggle = gtk_toggle_button_new_with_label(_("History"));

	rcw_history->paned = paned;
	rcw_history->list_side = list_side;
	rcw_history->toggle = toggle;
	gtk_paned_pack1(GTK_PANED(paned), list_side, TRUE, FALSE);
	/* Shrinkable: its width does not hold the window wide; the divider keeps it readable */
	gtk_paned_pack2(GTK_PANED(paned), view, TRUE, TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(toggle), "flat");
	gtk_widget_set_tooltip_text(toggle, _("Show or hide the history"));
	g_signal_connect(toggle, "toggled", G_CALLBACK(rcw_home_split_on_toggle), NULL);
	gtk_box_pack_end(GTK_BOX(tools_row), toggle, FALSE, FALSE, 0);
	gtk_widget_set_no_show_all(toggle, TRUE);
	g_signal_connect(paned, "size-allocate", G_CALLBACK(rcw_home_split_on_size_allocate), NULL);
	g_signal_connect(paned, "notify::position", G_CALLBACK(rcw_home_split_on_position), NULL);
	gtk_widget_show(list_side);
	gtk_widget_show(paned);
	return paned;
}

/* The profiles changed (renamed, deleted): History shows them as they are */
void rcw_history_refresh(void)
{
	rcw_sessions_refresh_all();
}

/* ☰ "History", the tray, the start page: the home tab of the current window, the focus in History */
void rcw_open_history(void)
{
	rcw_main_set_sidebar_visible(TRUE);
	rcw_main_present();
	if (!rcw_history)
		return;
	if (rcw_history->narrow)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rcw_history->toggle), TRUE);
	rcw_history->focus_index = 0;
	rcw_history_fill();
}

/* ---- The start page: continue where you left off ---- */

static void rcw_start_page_close(GtkWidget *button)
{
	GtkWidget *page = g_object_get_data(G_OBJECT(button), "start-page-widget");
	GtkWidget *notebook = page ? gtk_widget_get_parent(page) : NULL;

	if (notebook && GTK_IS_NOTEBOOK(notebook))
		gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page));
}

static void rcw_start_on_continue(GtkButton *button, gpointer data)
{
	rcw_start_page_close(GTK_WIDGET(button));
	rcw_continue_sessions();
}

static void rcw_start_on_sessions(GtkButton *button, gpointer data)
{
	rcw_start_page_close(GTK_WIDGET(button));
	g_clear_pointer(&rcw_continue_set, g_ptr_array_unref);
	rcw_open_history();
}

static void rcw_start_on_start_over(GtkButton *button, gpointer data)
{
	rcw_start_page_close(GTK_WIDGET(button));
	/* The sessions stay among the recently closed */
	g_clear_pointer(&rcw_continue_set, g_ptr_array_unref);
}

static GtkWidget *rcw_start_button(GtkWidget *box, GtkWidget *page, const gchar *label, GCallback cb, const gchar *style)
{
	GtkWidget *button = gtk_button_new_with_mnemonic(label);

	g_object_set_data(G_OBJECT(button), "start-page-widget", page);
	if (style)
		gtk_style_context_add_class(gtk_widget_get_style_context(button), style);
	g_signal_connect(button, "clicked", cb, NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	return button;
}

/* After the start: the windows of the last quit can come back */
static void rcw_start_page_add(RemminaConnectionWindow *cnnwin)
{
	GtkNotebook *notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	GtkWidget *page, *box, *w, *buttons, *label;
	guint i, tabs = 0;
	gchar *text;

	for (i = 0; i < rcw_continue_set->len; i++)
		tabs += ((RemminaSession *)g_ptr_array_index(rcw_continue_set, i))->tabs->len;
	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_set_data(G_OBJECT(page), "start-page", GINT_TO_POINTER(1));
	g_object_set_data_full(G_OBJECT(page), "tool-title", g_strdup(_("Start")), g_free);
	g_object_set_data_full(G_OBJECT(page), "tool-icon", g_strdup("document-open-recent-symbolic"), g_free);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_vexpand(box, TRUE);
	gtk_box_pack_start(GTK_BOX(page), box, TRUE, TRUE, 0);
	w = gtk_image_new_from_icon_name("document-open-recent-symbolic", GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(w), 96);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("Continue Where You Left Off?"));
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "title-1");
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	/* TRANSLATORS: on the start page: how many windows and tabs "Continue" brings back */
	text = g_strdup_printf(_("Windows open last time: %u, tabs: %u"), rcw_continue_set->len, tabs);
	w = gtk_label_new(text);
	g_free(text);
	gtk_style_context_add_class(gtk_widget_get_style_context(w), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_set_homogeneous(GTK_BOX(buttons), TRUE);
	gtk_widget_set_halign(buttons, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(buttons, 12);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
	w = rcw_start_button(buttons, page, _("_Continue"), G_CALLBACK(rcw_start_on_continue), "suggested-action");
	rcw_start_button(buttons, page, _("_Recently Closed Sessions"), G_CALLBACK(rcw_start_on_sessions), NULL);
	rcw_start_button(buttons, page, _("_Saved Sessions"), G_CALLBACK(rcw_start_on_sessions), NULL);
	rcw_start_button(buttons, page, _("Start _Over"), G_CALLBACK(rcw_start_on_start_over), "flat");
	gtk_widget_show_all(page);
	label = rcw_tab_title_label_new(_("Start"));
	gtk_widget_show(label);
	gtk_notebook_append_page(notebook, page, label);
	/* In front, unless the application was started to open a connection */
	if (!rcw_has_connection_pages(cnnwin)) {
		gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, page));
		gtk_widget_grab_focus(w);
	}
}

static gboolean rcw_offer_continue_idle(gpointer data)
{
	rcw_continue_set = remmina_session_take_continue_set();
	/* They are among the recently closed now */
	rcw_sessions_refresh_all();
	if (rcw_continue_set->len == 0 || !rcw_main) {
		g_clear_pointer(&rcw_continue_set, g_ptr_array_unref);
		return G_SOURCE_REMOVE;
	}
	rcw_start_page_add(rcw_main);
	return G_SOURCE_REMOVE;
}

/* The first window shows: it takes the next colour, as every new window does, and offers to
 * continue (not before: "--encrypt-password" and the like start the application without showing
 * it, and must take neither a colour nor the sessions; "Continue" brings its session's colour) */
static void rcw_on_first_map(GtkWidget *widget, gpointer data)
{
	static gboolean offered = FALSE;
	RemminaConnectionWindow *cnnwin = RCW(widget);

	if (offered)
		return;
	offered = TRUE;
	if (cnnwin->priv && cnnwin->priv->session && !cnnwin->priv->session->color)
		rcw_set_accent(cnnwin, rcw_accents[remmina_session_next_color() % G_N_ELEMENTS(rcw_accents)].name);
	g_idle_add(rcw_offer_continue_idle, NULL);
}

/* A tab the user closes can come back (Meta+Shift+T) */
static void rcw_remember_closed_tab(GtkWidget *page)
{
	RemminaSessionTab *tab = rcw_session_tab_of_page(page);

	if (!tab)
		return;
	g_queue_push_tail(&rcw_closed_tabs, tab);
	while (g_queue_get_length(&rcw_closed_tabs) > RCW_CLOSED_TABS_KEPT)
		remmina_session_tab_free(g_queue_pop_head(&rcw_closed_tabs));
}

static void rcw_on_new_window_clicked(GtkButton *button, gpointer data)
{
	rcw_open_new_window();
}

/* Create an application window: header bar with the toolbar toggle, the tabs, "New window" and the
 * main menu; the home tab (the connection list while the window is current) and the tabbed
 * content area with the connection toolbar */
static RemminaConnectionWindow *rcw_create_main(void)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	RemminaConnectionWindowPriv *priv;
	GtkWidget *grid;
	GtkWidget *toolbar;
	GtkWidget *headerbar, *toggle, *image, *stack, *empty, *endbox, *w;
	GtkNotebook *notebook;
	gint width, height;

	cnnwin = rcw_new(FALSE, 0);
	priv = cnnwin->priv;
	priv->is_main = TRUE;
	rcw_windows = g_list_append(rcw_windows, cnnwin);
	if (!rcw_main)
		rcw_main = cnnwin;
	gtk_widget_set_name(GTK_WIDGET(cnnwin), "remmina-connection-window-scrolled");
	g_signal_connect(G_OBJECT(cnnwin), "notify::is-active", G_CALLBACK(rcw_on_is_active), NULL);
	/* Its session: a new one, unless a restored session takes its place (rcw_adopt_session) */
	priv->session = remmina_session_new();
	if (!rcw_windows->next)
		g_signal_connect(G_OBJECT(cnnwin), "map", G_CALLBACK(rcw_on_first_map), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "configure-event", G_CALLBACK(rcw_on_configure_session), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "window-state-event", G_CALLBACK(rcw_on_configure_session), NULL);

	/* Header bar (client side decoration), must be set before the window is realized */
	headerbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
	gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(headerbar), FALSE);
	toggle = gtk_toggle_button_new();
	image = gtk_image_new_from_icon_name(REMMINA_APP_ID "-sidebar-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(toggle), image);
	gtk_widget_set_tooltip_text(toggle, _("Show or hide the connection toolbar"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), !remmina_pref.hide_connection_toolbar);
	g_signal_connect(G_OBJECT(toggle), "toggled", G_CALLBACK(rcw_on_sidebar_toggle), cnnwin);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), toggle);
	/* One bar: the toolbar toggle, then the tabs (home, connections, tools) and the "+" button.
	 * As the custom title with hexpand the strip takes all the room after the toggle. */
	gtk_header_bar_set_custom_title(GTK_HEADER_BAR(headerbar), rcw_tabstrip_new(cnnwin));
	/* At the end: "New window", then the main menu (remmina_main.c puts it in the slot) */
	endbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	w = gtk_button_new_from_icon_name("window-new-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text(w, _("New Window (Meta+N)"));
	/* Its accessible name (the property: libatk is not linked directly) */
	g_object_set(gtk_widget_get_accessible(w), "accessible-name", _("New Window"), NULL);
	gtk_widget_set_focus_on_click(w, FALSE);
	g_signal_connect(G_OBJECT(w), "clicked", G_CALLBACK(rcw_on_new_window_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(endbox), w, FALSE, FALSE, 0);
	priv->menu_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(endbox), priv->menu_slot, FALSE, FALSE, 0);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(headerbar), endbox);
	gtk_widget_show_all(headerbar);
	gtk_window_set_titlebar(GTK_WINDOW(cnnwin), headerbar);
	priv->headerbar = headerbar;
	priv->sidebar_toggle = toggle;
	gtk_window_set_title(GTK_WINDOW(cnnwin), "UniDeskBrowser");

	gtk_widget_realize(GTK_WIDGET(cnnwin));

	width = remmina_pref.main_width > 400 ? remmina_pref.main_width : 1000;
	height = remmina_pref.main_height > 300 ? remmina_pref.main_height : 700;
	gtk_window_set_default_size(GTK_WINDOW(cnnwin), width, height);
	remmina_theme_apply();

	/* Create the toolbar */
	toolbar = rcw_create_toolbar(cnnwin, SCROLLED_WINDOW_MODE, FALSE);

	/* Create the notebook (the page signals need priv->notebook as soon as the first page is added) */
	notebook = rcw_create_notebook(cnnwin);
	priv->notebook = notebook;

	/* Content: the tabs, or an empty-state placeholder when there is none */
	stack = gtk_stack_new();
	gtk_stack_set_homogeneous(GTK_STACK(stack), TRUE);
	gtk_stack_add_named(GTK_STACK(stack), GTK_WIDGET(notebook), "tabs");
	empty = rcw_create_empty_state(cnnwin);
	gtk_stack_add_named(GTK_STACK(stack), empty, "empty");
	gtk_widget_set_hexpand(stack, TRUE);
	gtk_widget_set_vexpand(stack, TRUE);
	gtk_widget_show(stack);

	/* Create the grid container for toolbar+content and populate it */
	grid = gtk_grid_new();
	gtk_grid_attach(GTK_GRID(grid), stack, 0, 0, 1, 1);
	rcw_place_toolbar(GTK_TOOLBAR(toolbar), GTK_GRID(grid), stack, remmina_pref.toolbar_placement);

	/* The tabs fill the whole window; the list of connections is the first tab */
	gtk_container_add(GTK_CONTAINER(cnnwin), grid);
	gtk_widget_show(grid);
	priv->sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(priv->sidebar_box, "unidesk-home-page");
	g_object_set_data(G_OBJECT(priv->sidebar_box), "home-page", GINT_TO_POINTER(1));
	g_object_set_data_full(G_OBJECT(priv->sidebar_box), "tool-title", g_strdup(_("Connections")), g_free);
	priv->home_placeholder = rcw_create_home_placeholder();
	gtk_box_pack_start(GTK_BOX(priv->sidebar_box), priv->home_placeholder, TRUE, TRUE, 0);
	gtk_widget_show(priv->sidebar_box);
	/* known as the home page before it is appended: "page-added" rebuilds the tab strip */
	priv->home_page = priv->sidebar_box;
	{
		GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		GtkWidget *w = gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_MENU);
		gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
		w = rcw_tab_title_label_new(_("Connections"));
		gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
		gtk_widget_show_all(hbox);
		gtk_notebook_append_page(notebook, priv->sidebar_box, hbox);
		gtk_notebook_set_tab_reorderable(notebook, priv->sidebar_box, FALSE);
		gtk_widget_set_can_focus(gtk_widget_get_parent(hbox), FALSE);
	}

	/* Add drag capabilities to the toolbar */
	gtk_drag_source_set(GTK_WIDGET(toolbar), GDK_BUTTON1_MASK,
			    dnd_targets_tb, sizeof dnd_targets_tb / sizeof *dnd_targets_tb, GDK_ACTION_MOVE);
	g_signal_connect_after(GTK_WIDGET(toolbar), "drag-begin", G_CALLBACK(rcw_tb_drag_begin), NULL);
	g_signal_connect(GTK_WIDGET(toolbar), "drag-failed", G_CALLBACK(rcw_tb_drag_failed), cnnwin);

	/* Add drop capabilities to the drop/dest target for the toolbar (the content area) */
	gtk_drag_dest_set(stack, GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
			  dnd_targets_tb, sizeof dnd_targets_tb / sizeof *dnd_targets_tb, GDK_ACTION_MOVE);
	gtk_drag_dest_set_track_motion(stack, TRUE);
	g_signal_connect(stack, "drag-drop", G_CALLBACK(rcw_tb_drag_drop), cnnwin);

	priv->view_mode = SCROLLED_WINDOW_MODE;
	priv->toolbar = toolbar;
	priv->grid = grid;
	priv->notebook = notebook;
	priv->content_stack = stack;
	priv->empty_box = empty;
	priv->ss_width = width;
	priv->ss_height = height;
	priv->ss_maximized = remmina_pref.main_maximize;

	g_signal_connect(G_OBJECT(cnnwin), "set-focus", G_CALLBACK(rcw_on_set_focus), NULL);

	if (remmina_pref.main_maximize)
		gtk_window_maximize(GTK_WINDOW(cnnwin));

	rcw_update_empty_state(cnnwin);

	return cnnwin;
}

static void rcw_create_overlay_ftb_overlay(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	GtkWidget *revealer;
	RemminaConnectionWindowPriv *priv;

	priv = cnnwin->priv;


	if (priv->overlay_ftb_fr != NULL) {
		gtk_widget_destroy(priv->overlay_ftb_fr);
		priv->overlay_ftb_fr = NULL;
	}
	if (priv->overlay_ftb_overlay != NULL) {
		gtk_widget_destroy(priv->overlay_ftb_overlay);
		priv->overlay_ftb_overlay = NULL;
		priv->revealer = NULL;
	}


	rcw_create_floating_toolbar(cnnwin, priv->fss_view_mode);

	priv->overlay_ftb_overlay = gtk_event_box_new();

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);

	GtkWidget *handle = gtk_drawing_area_new();

	gtk_widget_set_size_request(handle, 4, 4);
	gtk_widget_set_name(handle, "ftb-handle");

	revealer = gtk_revealer_new();

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_TOP_LEFT) {
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_START);
	}
	else if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_TOP_RIGHT) {
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_END);
	}
	else{
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_CENTER);
	}

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT) {
		gtk_box_pack_start(GTK_BOX(vbox), handle, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), revealer, FALSE, FALSE, 0);
		gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
		gtk_widget_set_valign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_END);
	} else {
		gtk_box_pack_start(GTK_BOX(vbox), revealer, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), handle, FALSE, FALSE, 0);
		gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
		gtk_widget_set_valign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_START);
	}


	gtk_container_add(GTK_CONTAINER(revealer), priv->floating_toolbar_widget);
	gtk_widget_set_halign(GTK_WIDGET(revealer), GTK_ALIGN_CENTER);
	gtk_widget_set_valign(GTK_WIDGET(revealer), GTK_ALIGN_START);

	priv->revealer = revealer;

	GtkWidget *fr;

	fr = gtk_frame_new(NULL);
	priv->overlay_ftb_fr = fr;
	gtk_container_add(GTK_CONTAINER(priv->overlay_ftb_overlay), fr);
	gtk_container_add(GTK_CONTAINER(fr), vbox);

	gtk_widget_show(vbox);
	gtk_widget_show(revealer);
	gtk_widget_show(handle);
	gtk_widget_show(priv->overlay_ftb_overlay);
	gtk_widget_show(fr);

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT)
		gtk_widget_set_name(fr, "ftbbox-lower");
	else
		gtk_widget_set_name(fr, "ftbbox-upper");

	gtk_overlay_add_overlay(GTK_OVERLAY(priv->overlay), priv->overlay_ftb_overlay);

	rcw_floating_toolbar_show(cnnwin, TRUE);

	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "enter-notify-event", G_CALLBACK(rcw_floating_toolbar_on_enter), cnnwin);
	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "leave-notify-event", G_CALLBACK(rcw_floating_toolbar_on_leave), cnnwin);
	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "scroll-event", G_CALLBACK(rcw_floating_toolbar_on_scroll), cnnwin);
	gtk_widget_add_events(
		GTK_WIDGET(priv->overlay_ftb_overlay),
		GDK_SCROLL_MASK
#if GTK_CHECK_VERSION(3, 4, 0)
		| GDK_SMOOTH_SCROLL_MASK
#endif
		);

	/* Add drag and drop capabilities to the source */
	gtk_drag_source_set(GTK_WIDGET(priv->overlay_ftb_overlay), GDK_BUTTON1_MASK,
			    dnd_targets_ftb, sizeof dnd_targets_ftb / sizeof *dnd_targets_ftb, GDK_ACTION_MOVE);
	g_signal_connect_after(GTK_WIDGET(priv->overlay_ftb_overlay), "drag-begin", G_CALLBACK(rcw_ftb_drag_begin), cnnwin);

	if (remmina_pref.fullscreen_toolbar_visibility == FLOATING_TOOLBAR_VISIBILITY_DISABLE)
		/* toolbar in fullscreenmode disabled, hide everything */
		gtk_widget_hide(fr);
}


static gboolean rcw_ftb_drag_drop(GtkWidget *widget, GdkDragContext *context,
				  gint x, gint y, guint time, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gint width, height;
	gint new_floating_toolbar_placement;
	RemminaConnectionObject *cnnobj;

	/* Only the floating toolbar itself moves it, not files dragged over the window */
	if (gtk_drag_dest_find_target(widget, context, NULL) == GDK_NONE)
		return FALSE;

	gtk_window_get_size(GTK_WINDOW(cnnwin), &width, &height);

	if (y >= height / 2 && x < width / 3)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT;
	else if (y >= height / 2 && x > (2*(width / 3)))
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT;
	else if (y < height / 2 && x > (2*(width / 3)))
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP_RIGHT;
	else if (y < height / 2 && x  < width / 3)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP_LEFT;
	else if (y >= height /2)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM;
	else
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP;

	gtk_drag_finish(context, TRUE, TRUE, time);

	if (new_floating_toolbar_placement != remmina_pref.floating_toolbar_placement) {
		/* Destroy and recreate the FTB */
		remmina_pref.floating_toolbar_placement = new_floating_toolbar_placement;
		remmina_pref_save();
		rcw_create_overlay_ftb_overlay(cnnwin);
		cnnobj = rcw_get_visible_cnnobj(cnnwin);
		if (cnnobj) {
			rco_update_toolbar(cnnobj);
		}
	}
	return TRUE;
}

static void rcw_ftb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	TRACE_CALL(__func__);

	cairo_surface_t *surface;
	cairo_t *cr;
	GtkAllocation wa;
	double dashes[] = { 10 };

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow*)user_data;
	guint hmargin = abs(cnnwin->priv->ftb_move_horiz);
	guint vmargin = abs(cnnwin->priv->ftb_move_vert);

	gtk_widget_get_allocation(widget, &wa);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, wa.width - hmargin, wa.height - vmargin);
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
	cairo_set_line_width(cr, 2);
	cairo_set_dash(cr, dashes, 1, 0);
	cairo_rectangle(cr, 0, 0, wa.width, wa.height);
	cairo_stroke(cr);
	cairo_destroy(cr);

	gtk_drag_set_icon_surface(context, surface);
}

RemminaConnectionWindow *rcw_create_fullscreen(GtkWindow *old, gint view_mode)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	GtkNotebook *notebook;

#if GTK_CHECK_VERSION(3, 22, 0)
	gint n_monitors;
	gint i;
	GdkMonitor *old_monitor;
	GdkDisplay *old_display;
	GdkWindow *old_window;
#endif
	gint full_screen_target_monitor;

	full_screen_target_monitor = FULL_SCREEN_TARGET_MONITOR_UNDEFINED;
	if (old) {
#if GTK_CHECK_VERSION(3, 22, 0)
		old_window = gtk_widget_get_window(GTK_WIDGET(old));
		old_display = gdk_window_get_display(old_window);
		old_monitor = gdk_display_get_monitor_at_window(old_display, old_window);
		n_monitors = gdk_display_get_n_monitors(old_display);
		for (i = 0; i < n_monitors; ++i) {
			if (gdk_display_get_monitor(old_display, i) == old_monitor) {
				full_screen_target_monitor = i;
				break;
			}
		}
#else
		full_screen_target_monitor = gdk_screen_get_monitor_at_window(
			gdk_screen_get_default(),
			gtk_widget_get_window(GTK_WIDGET(old)));
#endif
	}

	cnnwin = rcw_new(TRUE, full_screen_target_monitor);

	gtk_widget_set_name(GTK_WIDGET(cnnwin), "remmina-connection-window-fullscreen");
	gtk_widget_realize(GTK_WIDGET(cnnwin));

	if (!view_mode)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	notebook = rcw_create_notebook(cnnwin);

	cnnwin->priv->overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(cnnwin), cnnwin->priv->overlay);
	gtk_container_add(GTK_CONTAINER(cnnwin->priv->overlay), GTK_WIDGET(notebook));
	gtk_widget_show(GTK_WIDGET(cnnwin->priv->overlay));

	cnnwin->priv->notebook = notebook;
	cnnwin->priv->view_mode = view_mode;
	cnnwin->priv->fss_view_mode = view_mode;

	/* Create the floating toolbar */
	rcw_create_overlay_ftb_overlay(cnnwin);
	/* Add drag and drop capabilities to the drop/dest target for floating toolbar */
	gtk_drag_dest_set(GTK_WIDGET(cnnwin->priv->overlay), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
			  dnd_targets_ftb, sizeof dnd_targets_ftb / sizeof *dnd_targets_ftb, GDK_ACTION_MOVE);
	gtk_drag_dest_set_track_motion(GTK_WIDGET(cnnwin->priv->notebook), TRUE);
	g_signal_connect(GTK_WIDGET(cnnwin->priv->overlay), "drag-drop", G_CALLBACK(rcw_ftb_drag_drop), cnnwin);

	gtk_widget_show(GTK_WIDGET(cnnwin));
	GtkWindowGroup *wingrp = gtk_window_group_new();
	gtk_window_group_add_window(wingrp, GTK_WINDOW(cnnwin));
	gtk_window_set_transient_for(GTK_WINDOW(cnnwin), NULL);

	return cnnwin;
}

static gboolean rcw_hostkey_func(RemminaProtocolWidget *gp, guint keyval, gboolean release)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	const RemminaProtocolFeature *feature;
	gint i;

	if (release) {
		if (remmina_pref.hostkey && keyval == remmina_pref.hostkey) {
			priv->hostkey_activated = FALSE;
			if (priv->hostkey_used)
				/* hostkey pressed + something else */
				return TRUE;
		}
		/* If hostkey is released without pressing other keys, we should execute the
		 * shortcut key which is the same as hostkey. Be default, this is grab/ungrab
		 * keyboard */
		else if (priv->hostkey_activated) {
			/* Trap all key releases when hostkey is pressed */
			/* hostkey pressed + something else */
			return TRUE;
		} else {
			/* Any key pressed, no hostkey */
			return FALSE;
		}
	} else if (remmina_pref.hostkey && keyval == remmina_pref.hostkey) {
		/** @todo Add callback for hostname transparent overlay #832 */
		priv->hostkey_activated = TRUE;
		priv->hostkey_used = FALSE;
		return TRUE;
	} else if (!priv->hostkey_activated) {
		/* Any key pressed, no hostkey */
		return FALSE;
	}

	priv->hostkey_used = TRUE;
	keyval = gdk_keyval_to_lower(keyval);
	if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down
	    || keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) {
		GtkAdjustment *adjust;
		int pos;

		if (cnnobj->connected && GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down)
				adjust = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
			else
				adjust = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Left)
				pos = 0;
			else
				pos = gtk_adjustment_get_upper(adjust);

			gtk_adjustment_set_value(adjust, pos);
			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down)
				gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), adjust);
			else
				gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), adjust);
		} else if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container)) {
			RemminaScrolledViewport *gsv;
			GtkWidget *child;
			GdkWindow *gsvwin;
			gint sz;
			GtkAdjustment *adj;
			gdouble value;

			if (!GTK_IS_BIN(cnnobj->scrolled_container))
				return FALSE;

			gsv = REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container);
			child = gtk_bin_get_child(GTK_BIN(gsv));
			if (!GTK_IS_VIEWPORT(child))
				return FALSE;

			gsvwin = gtk_widget_get_window(GTK_WIDGET(gsv));
			if (!gsv)
				return FALSE;

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
				sz = gdk_window_get_height(gsvwin) + 2; // Add 2px of black scroll border
				adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(child));
			} else {
				sz = gdk_window_get_width(gsvwin) + 2; // Add 2px of black scroll border
				adj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(child));
			}

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Left)
				value = 0;
			else
				value = gtk_adjustment_get_upper(GTK_ADJUSTMENT(adj)) - (gdouble)sz + 2.0;

			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj), value);
		}
	}

	if (keyval == remmina_pref.shortcutkey_fullscreen && !extrahardening) {
		switch (priv->view_mode) {
		case SCROLLED_WINDOW_MODE:
			rcw_switch_viewmode(cnnobj->cnnwin, priv->fss_view_mode);
			break;
		case SCROLLED_FULLSCREEN_MODE:
		case VIEWPORT_FULLSCREEN_MODE:
			rcw_switch_viewmode(cnnobj->cnnwin, SCROLLED_WINDOW_MODE);
			break;
		default:
			break;
		}
	} else if (keyval == remmina_pref.shortcutkey_autofit && !extrahardening) {
		if (priv->toolitem_autofit && gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_autofit)))
			rcw_toolbar_autofit(GTK_TOOL_ITEM(gp), cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_nexttab && !extrahardening) {
		i = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)) + 1;
		if (i >= gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)))
			i = 0;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), i);
	} else if (keyval == remmina_pref.shortcutkey_prevtab && !extrahardening) {
		i = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)) - 1;
		if (i < 0)
			i = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)) - 1;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), i);
	} else if (keyval == remmina_pref.shortcutkey_clipboard && !extrahardening) {
		if (remmina_protocol_widget_plugin_receives_keystrokes(REMMINA_PROTOCOL_WIDGET(cnnobj->proto))) {
			remmina_protocol_widget_send_clipboard((RemminaProtocolWidget*)cnnobj->proto, G_OBJECT(cnnobj->proto));
		}
	} else if (keyval == remmina_pref.shortcutkey_scale && !extrahardening) {
		if (gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_scale))) {
			gtk_toggle_tool_button_set_active(
				GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale),
				!gtk_toggle_tool_button_get_active(
					GTK_TOGGLE_TOOL_BUTTON(
						priv->toolitem_scale)));
		}
	} else if (keyval == remmina_pref.shortcutkey_grab && !extrahardening) {
		gtk_toggle_tool_button_set_active(
			GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_grab),
			!gtk_toggle_tool_button_get_active(
				GTK_TOGGLE_TOOL_BUTTON(
					priv->toolitem_grab)));
	} else if (keyval == remmina_pref.shortcutkey_invert_scroll && !extrahardening) {
		if (priv->toolitem_invert_scroll && gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_invert_scroll))) {
			gtk_toggle_tool_button_set_active(
				GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_invert_scroll),
				!gtk_toggle_tool_button_get_active(
					GTK_TOGGLE_TOOL_BUTTON(
						priv->toolitem_invert_scroll)));
		}
	} else if (keyval == remmina_pref.shortcutkey_minimize && !extrahardening) {
		rcw_toolbar_minimize(GTK_TOOL_ITEM(gp),
				     cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_viewonly && !extrahardening) {
		remmina_file_set_int(cnnobj->remmina_file, "viewonly",
				     (remmina_file_get_int(cnnobj->remmina_file, "viewonly", 0)
				      == 0) ? 1 : 0);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_viewonly),
					remmina_file_get_int(cnnobj->remmina_file, "viewonly", 0));
	} else if (keyval == remmina_pref.shortcutkey_screenshot && !extrahardening) {
		rcw_toolbar_screenshot(GTK_TOOL_ITEM(gp),
				       cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_disconnect && !extrahardening) {
		rco_disconnect_current_page(cnnobj);
	} else if (keyval == remmina_pref.shortcutkey_toolbar && !extrahardening) {
		if (priv->view_mode == SCROLLED_WINDOW_MODE) {
			remmina_pref.hide_connection_toolbar =
				!remmina_pref.hide_connection_toolbar;
			rcw_set_toolbar_visibility(cnnobj->cnnwin);
		}
	} else {
		for (feature =
			     remmina_protocol_widget_get_features(
				     REMMINA_PROTOCOL_WIDGET(
					     cnnobj->proto));
		     feature && feature->type;
		     feature++) {
			if (feature->type
			    == REMMINA_PROTOCOL_FEATURE_TYPE_TOOL
			    && GPOINTER_TO_UINT(
				    feature->opt3)
			    == keyval) {
				remmina_protocol_widget_call_feature_by_ref(
					REMMINA_PROTOCOL_WIDGET(
						cnnobj->proto),
					feature);
				break;
			}
		}
	}
	/* If a keypress makes the current cnnobj to move to another window,
	 * priv is now invalid. So we can no longer use priv here */
	cnnobj->cnnwin->priv->hostkey_activated = FALSE;

	/* Trap all keypresses when hostkey is pressed */
	return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Unidesk: the single application window and its browser-like tabs           */
/* ------------------------------------------------------------------------- */

/* A page that is not a connection: a hosted tool window, the "Connections" tab, the start page
 * or a restored tab that has not connected yet */
static gboolean rcw_page_is_tool(GtkWidget *page)
{
	return page != NULL && (g_object_get_data(G_OBJECT(page), "tool-window") != NULL ||
				g_object_get_data(G_OBJECT(page), "home-page") != NULL ||
				g_object_get_data(G_OBJECT(page), "start-page") != NULL ||
				g_object_get_data(G_OBJECT(page), "session-tab") != NULL);
}

static const gchar *rcw_tool_page_title(GtkWidget *page)
{
	const gchar *t = page ? (const gchar *)g_object_get_data(G_OBJECT(page), "tool-title") : NULL;

	return t ? t : "";
}

static gboolean rcw_widget_in_sidebar(RemminaConnectionWindow *cnnwin, GtkWidget *w)
{
	if (!w || !cnnwin->priv || !cnnwin->priv->sidebar_box)
		return FALSE;
	return gtk_widget_is_ancestor(w, cnnwin->priv->sidebar_box);
}

/* Keyboard focus moved inside the application window: never keep the keyboard
 * grabbed while the user works in the sidebar or in a tool tab */
static void rcw_on_set_focus(GtkWindow *window, GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = RCW(window);

	if (!cnnwin->priv)
		return;
	if (rcw_widget_in_sidebar(cnnwin, widget) || !rcw_get_visible_cnnobj(cnnwin))
		rcw_kp_ungrab(cnnwin);
}

/* Header bar button: show or hide the connection toolbar */
static void rcw_on_sidebar_toggle(GtkToggleButton *btn, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	remmina_pref.hide_connection_toolbar = !gtk_toggle_button_get_active(btn);
	rcw_update_empty_state(cnnwin);
	remmina_pref_save();
}

RemminaConnectionWindow *rcw_get_main(void)
{
	if (!rcw_main)
		rcw_create_main();
	return rcw_main;
}

RemminaConnectionWindow *rcw_peek_main(void)
{
	return rcw_main;
}

/* "New window" (Meta+N): an empty session with its home tab; it becomes the current window */
/* A new window; with a colour: the next of the palette, round and round, so windows opened one after
 * another are told apart (the first window takes its colour when it shows: rcw_on_first_map) */
static RemminaConnectionWindow *rcw_open_new_window_colored(gboolean colored)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;

	if (!rcw_main)
		return rcw_get_main();
	cnnwin = rcw_create_main();
	if (colored)
		rcw_set_accent(cnnwin, rcw_accents[remmina_session_next_color() % G_N_ELEMENTS(rcw_accents)].name);
	gtk_widget_show(GTK_WIDGET(cnnwin));
	rcw_set_current(cnnwin);
	gtk_window_present(GTK_WINDOW(cnnwin));
	return cnnwin;
}

RemminaConnectionWindow *rcw_open_new_window(void)
{
	return rcw_open_new_window_colored(TRUE);
}

GtkWidget *rcw_get_home_box(RemminaConnectionWindow *cnnwin)
{
	return cnnwin && cnnwin->priv ? cnnwin->priv->sidebar_box : NULL;
}

GtkWidget *rcw_get_menu_slot(RemminaConnectionWindow *cnnwin)
{
	return cnnwin && cnnwin->priv ? cnnwin->priv->menu_slot : NULL;
}

GtkWidget *rcw_main_get_menu_slot(void)
{
	return rcw_get_main()->priv->menu_slot;
}

/* The connection list came into (or left) this window's home tab */
void rcw_home_changed(RemminaConnectionWindow *cnnwin)
{
	if (cnnwin && cnnwin->priv)
		rcw_update_home_placeholder(cnnwin);
}

/* The application is quitting: windows close together, none is "closed alone" */
void rcw_set_app_quitting(void)
{
	rcw_app_quitting = TRUE;
	rcw_app_exiting = TRUE;
	rcw_session_freeze_all();
}

GtkWidget *rcw_main_get_sidebar_box(void)
{
	return rcw_get_main()->priv->sidebar_box;
}

GtkWidget *rcw_main_get_headerbar(void)
{
	return rcw_get_main()->priv->headerbar;
}

/* TRUE when the "Connections" tab is the current one */
gboolean rcw_main_get_sidebar_visible(void)
{
	RemminaConnectionWindowPriv *priv;
	gint cur;

	if (!rcw_main || !rcw_main->priv || !rcw_main->priv->home_page)
		return FALSE;
	priv = rcw_main->priv;
	cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook));
	return cur >= 0 && gtk_notebook_get_nth_page(GTK_NOTEBOOK(priv->notebook), cur) == priv->home_page;
}

/* Switch to the "Connections" tab (FALSE does nothing: the tab always exists) */
void rcw_main_set_sidebar_visible(gboolean visible)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *w = rcw_get_main();
	RemminaConnectionWindowPriv *priv = w->priv;
	gint n;

	if (!visible || !priv->home_page)
		return;
	n = gtk_notebook_page_num(GTK_NOTEBOOK(priv->notebook), priv->home_page);
	if (n >= 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), n);
}

void rcw_main_toggle_sidebar(void)
{
	rcw_main_set_sidebar_visible(TRUE);
}

void rcw_main_present(void)
{
	RemminaConnectionWindow *w = rcw_get_main();

	gtk_widget_show(GTK_WIDGET(w));
	gtk_window_deiconify(GTK_WINDOW(w));
	gtk_window_present(GTK_WINDOW(w));
}

void rcw_main_save_geometry(void)
{
	RemminaConnectionWindowPriv *priv;
	GdkWindow *gdkwin;
	gint width, height;

	if (!rcw_main || !rcw_main->priv)
		return;
	priv = rcw_main->priv;
	gdkwin = gtk_widget_get_window(GTK_WIDGET(rcw_main));
	if (gdkwin && (gdk_window_get_state(gdkwin) & GDK_WINDOW_STATE_MAXIMIZED)) {
		remmina_pref.main_maximize = TRUE;
	} else if (gdkwin) {
		gtk_window_get_size(GTK_WINDOW(rcw_main), &width, &height);
		remmina_pref.main_width = width;
		remmina_pref.main_height = height;
		remmina_pref.main_maximize = FALSE;
	}
}

/* Tool tabs: a toplevel window (profile editor, preferences, about…) hosted as a tab */

static void rcw_tool_page_destroyed(GtkWidget *page, gpointer user_data)
{
	GtkWidget *win = (GtkWidget *)user_data;

	if (G_IS_OBJECT(win))
		g_object_set_data(G_OBJECT(win), "tool-page", NULL);
}

static void rcw_tool_window_destroyed(GtkWidget *win, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkWidget *page = (GtkWidget *)g_object_get_data(G_OBJECT(win), "tool-page");
	GtkWidget *parent;

	if (!page || !GTK_IS_WIDGET(page))
		return;
	g_object_set_data(G_OBJECT(win), "tool-page", NULL);
	parent = gtk_widget_get_parent(page);
	if (parent && GTK_IS_NOTEBOOK(parent))
		gtk_notebook_remove_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), page));
	else
		gtk_widget_destroy(page);
}

static void rcw_tool_tab_close_clicked(GtkButton *button, gpointer user_data)
{
	GtkWidget *win = (GtkWidget *)user_data;

	if (GTK_IS_WIDGET(win))
		gtk_widget_destroy(win);
}

static GtkWidget *rcw_create_tool_tab_label(const gchar *title, const gchar *icon_name, GtkWidget *win)
{
	GtkWidget *hbox, *widget, *button;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_show(hbox);

	if (icon_name && icon_name[0]) {
		widget = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
		gtk_widget_show(widget);
		gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	}

	widget = rcw_tab_title_label_new(title);
	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(button, FALSE);
	gtk_widget_set_name(button, "remmina-small-button");
	gtk_widget_show(button);
	widget = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_container_add(GTK_CONTAINER(button), widget);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(rcw_tool_tab_close_clicked), win);

	return hbox;
}

/* ---- The header bar tab strip ---- */

/* A tab toggle was activated: make its page current */
static void rcw_tabstrip_on_tab_toggled(GtkToggleButton *btn, RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *page;
	gint n;

	if (!priv || priv->tabstrip_updating || !gtk_toggle_button_get_active(btn))
		return;
	page = (GtkWidget *)g_object_get_data(G_OBJECT(btn), "page");
	if (!page || !priv->notebook || !GTK_IS_NOTEBOOK(priv->notebook))
		return;
	n = gtk_notebook_page_num(GTK_NOTEBOOK(priv->notebook), page);
	if (n >= 0 && n != gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)))
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), n);
}

/* The "x" of a tab: close the connection(s) of the page, or the tool window it hosts */
static void rcw_tabstrip_on_close_clicked(GtkButton *button, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkWidget *page = (GtkWidget *)user_data;
	RemminaConnectionObject *cnnobj;
	GtkWidget *win;

	if (!page || !GTK_IS_WIDGET(page))
		return;
	cnnobj = g_object_get_data(G_OBJECT(page), "active-cnnobj");
	if (!cnnobj)
		cnnobj = g_object_get_data(G_OBJECT(page), "cnnobj");
	if (cnnobj) {
		rcw_remember_closed_tab(page);
		rco_on_close_button_clicked(button, cnnobj);
		return;
	}
	if (g_object_get_data(G_OBJECT(page), "session-tab") || g_object_get_data(G_OBJECT(page), "start-page")) {
		/* A restored tab not connected yet or the start page: just the page */
		GtkWidget *notebook = gtk_widget_get_parent(page);
		if (g_object_get_data(G_OBJECT(page), "session-tab"))
			rcw_remember_closed_tab(page);
		if (GTK_IS_NOTEBOOK(notebook))
			gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page));
		return;
	}
	win = (GtkWidget *)g_object_get_data(G_OBJECT(page), "tool-window");
	if (win && GTK_IS_WIDGET(win))
		gtk_widget_destroy(win);
}

/* The keyboard is grabbed for the remote side (Right Ctrl, or the profile's "grab all keyboard
 * events"): the local shortcuts (Meta+W, N, T, Shift+T) step aside and the keys go to the server */
static gboolean rcw_keyboard_is_grabbed(RemminaConnectionWindow *cnnwin)
{
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(cnnwin));
	GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
	GdkDevice *keyboard = seat ? gdk_seat_get_keyboard(seat) : NULL;

	return keyboard && gdk_display_device_is_grabbed(display, keyboard);
}

/* Meta+W: what the "x" of the current tab does; the home tab has none */
static gboolean rcw_accel_close_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)data;
	GtkNotebook *notebook;
	GtkWidget *page;

	if (rcw_keyboard_is_grabbed(cnnwin))
		return FALSE;
	if (!cnnwin->priv || !cnnwin->priv->notebook || !GTK_IS_NOTEBOOK(cnnwin->priv->notebook))
		return FALSE;
	notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);
	page = gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook));
	if (page && page != cnnwin->priv->home_page && !g_object_get_data(G_OBJECT(page), "home-page"))
		rcw_tabstrip_on_close_clicked(NULL, page);
	return TRUE;
}

/* Meta+N: a new window (an empty session) */
static gboolean rcw_accel_new_window(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data)
{
	TRACE_CALL(__func__);
	if (rcw_keyboard_is_grabbed((RemminaConnectionWindow *)data))
		return FALSE;
	rcw_open_new_window();
	return TRUE;
}

/* Meta+T: the home tab, where a new connection starts (each window has one, as the page of a new tab) */
static gboolean rcw_accel_home_tab(GtkAccelGroup *group, GObject *acceleratable, guint keyval, GdkModifierType mods, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)data;
	gint n;

	if (rcw_keyboard_is_grabbed(cnnwin) || !cnnwin->priv || !cnnwin->priv->home_page || !cnnwin->priv->notebook)
		return FALSE;
	n = gtk_notebook_page_num(GTK_NOTEBOOK(cnnwin->priv->notebook), cnnwin->priv->home_page);
	if (n >= 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(cnnwin->priv->notebook), n);
	return TRUE;
}

/* ---- Moving tabs between windows: the tab menu and drag and drop ---- */

static const GtkTargetEntry rcw_tab_targets[] = {
	{ (gchar *)"application/x-unidesk-tab", GTK_TARGET_SAME_APP, 0 },
};
static GtkWidget *rcw_dragged_page = NULL;

/* A connection tab (or a restored one not connected yet) can change window; tool tabs cannot */
static gboolean rcw_page_can_move(GtkWidget *page)
{
	return page && (g_object_get_data(G_OBJECT(page), "cnnobj") || g_object_get_data(G_OBJECT(page), "session-tab"));
}

static void rcw_move_tab(GtkWidget *page, RemminaConnectionWindow *to)
{
	GtkWidget *top = gtk_widget_get_toplevel(page);
	RemminaConnectionWindow *from = REMMINA_IS_CONNECTION_WINDOW(top) ? RCW(top) : NULL;
	GtkNotebook *notebook;
	GtkWidget *newpage;

	if (!from || from == to || !from->priv || !to || !to->priv || !rcw_page_can_move(page))
		return;
	notebook = GTK_NOTEBOOK(to->priv->notebook);
	newpage = rcw_migrate_page(from, to, page);
	if (newpage)
		gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, newpage));
	rcw_set_current(to);
	gtk_window_present(GTK_WINDOW(to));
}

/* Two windows exchange their sessions (with colour and name) */
static void rcw_session_swap(RemminaConnectionWindow *a, RemminaConnectionWindow *b)
{
	RemminaSession *s = a->priv->session;
	gboolean stored = a->priv->session_stored;

	if (a->priv->session_save_id) {
		g_source_remove(a->priv->session_save_id);
		a->priv->session_save_id = 0;
	}
	if (b->priv->session_save_id) {
		g_source_remove(b->priv->session_save_id);
		b->priv->session_save_id = 0;
	}
	a->priv->session = b->priv->session;
	a->priv->session_stored = b->priv->session_stored;
	b->priv->session = s;
	b->priv->session_stored = stored;
	rcw_apply_accent(a);
	rcw_apply_accent(b);
}

/* A tab gets a window of its own (of the size of the window it leaves; at x, y when place is set).
 * The only tab of its window: the window rather moves, the new one goes on with its session, and
 * the old one, empty, closes. */
static void rcw_move_tab_to_new_window(GtkWidget *page, gboolean place, gint x, gint y)
{
	GtkWidget *top = gtk_widget_get_toplevel(page);
	RemminaConnectionWindow *from = REMMINA_IS_CONNECTION_WINDOW(top) ? RCW(top) : NULL;
	RemminaConnectionWindow *to;
	gint width, height;

	if (!from || !from->priv || !rcw_page_can_move(page))
		return;
	/* The only tab takes the session (and its colour) along: no new colour then */
	to = rcw_open_new_window_colored(!(from->priv->is_main && rcw_count_tabs(from) == 1));
	if (!to || to == from || !to->priv)
		return;
	if (from->priv->is_main) {
		gtk_window_get_size(GTK_WINDOW(from), &width, &height);
		gtk_window_resize(GTK_WINDOW(to), width, height);
		if (rcw_count_tabs(from) == 1 && from->priv->session && to->priv->session)
			rcw_session_swap(from, to);
	}
	if (place)
		gtk_window_move(GTK_WINDOW(to), MAX(0, x - 120), MAX(0, y - 20));
	rcw_move_tab(page, to);
}

static void rcw_tab_menu_on_move_new_window(GtkMenuItem *item, GtkWidget *page)
{
	rcw_move_tab_to_new_window(page, FALSE, 0, 0);
}

static void rcw_tab_menu_on_close(GtkMenuItem *item, GtkWidget *page)
{
	rcw_tabstrip_on_close_clicked(NULL, page);
}

/* The colours under a tab of the strip (the strip may have been rebuilt meanwhile) */
static gboolean rcw_tab_accent_idle(gpointer data)
{
	GtkWidget *btn = data;
	GtkWidget *top = gtk_widget_get_toplevel(btn);

	if (gtk_widget_get_mapped(btn) && REMMINA_IS_CONNECTION_WINDOW(top) && RCW(top)->priv)
		rcw_show_accent_popover(btn, RCW(top));
	g_object_unref(btn);
	return G_SOURCE_REMOVE;
}

static void rcw_tab_menu_on_accent(GtkMenuItem *item, GtkWidget *btn)
{
	g_idle_add(rcw_tab_accent_idle, g_object_ref(btn));
}

/* The session of the window that holds the tab */
static void rcw_tab_menu_on_rename_session(GtkMenuItem *item, GtkWidget *btn)
{
	GtkWidget *top = gtk_widget_get_toplevel(btn);

	if (REMMINA_IS_CONNECTION_WINDOW(top) && RCW(top)->priv && RCW(top)->priv->session)
		rcw_session_rename(GTK_WINDOW(top), RCW(top)->priv->session->id);
}

/* Right click on the home tab: the window colour */
static gboolean rcw_home_tab_on_button_press(GtkWidget *btn, GdkEventButton *event, gpointer data)
{
	if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
		return FALSE;
	rcw_tab_menu_on_accent(NULL, btn);
	return TRUE;
}

/* Right click on a tab: as in browsers (GNOME HIG: "Close" last) */
static RemminaConnectionObject *rcw_cnnobj_of_page(GtkWidget *page)
{
	RemminaConnectionObject *cnnobj = g_object_get_data(G_OBJECT(page), "active-cnnobj");

	return cnnobj ? cnnobj : g_object_get_data(G_OBJECT(page), "cnnobj");
}

/* SSH ↔ SFTP: the same server in a new tab right after this one. The plugin passes on the
 * credentials the tab connected with and its folder (remmina_ssh_plugin.c, remmina_sftp_plugin.c);
 * the new tab remembers the profile it came from, for the sessions and the frequently used */
static void rcw_tab_menu_on_sibling(GtkMenuItem *item, GtkWidget *page)
{
	RemminaConnectionObject *cnnobj = rcw_cnnobj_of_page(page), *opened;
	const gchar *protocol = g_object_get_data(G_OBJECT(item), "protocol");
	GtkWidget *top = gtk_widget_get_toplevel(page), *newpage;
	GtkNotebook *notebook;
	const gchar *source;
	gint before;

	if (!cnnobj || !cnnobj->proto || !REMMINA_IS_CONNECTION_WINDOW(top) || !RCW(top)->priv)
		return;
	notebook = GTK_NOTEBOOK(RCW(top)->priv->notebook);
	before = gtk_notebook_get_n_pages(notebook);
	rcw_open_target = RCW(top);
	remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
						     g_strcmp0(protocol, "SFTP") == 0 ? REMMINA_PROTOCOL_FEATURE_TOOL_SFTP : REMMINA_PROTOCOL_FEATURE_TOOL_SSH);
	rcw_open_target = NULL;
	if (gtk_notebook_get_n_pages(notebook) <= before)
		return;
	/* The connection came last */
	newpage = gtk_notebook_get_nth_page(notebook, gtk_notebook_get_n_pages(notebook) - 1);
	opened = rcw_cnnobj_of_page(newpage);
	source = remmina_file_get_filename(cnnobj->remmina_file);
	if (!remmina_db_is_id(source))
		source = remmina_file_get_string(cnnobj->remmina_file, "unidesk_source_profile");
	if (opened && opened->remmina_file && remmina_db_is_id(source))
		remmina_file_set_string(opened->remmina_file, "unidesk_source_profile", source);
	gtk_notebook_reorder_child(notebook, newpage, gtk_notebook_page_num(notebook, page) + 1);
	gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, newpage));
}

static gboolean rcw_tabstrip_on_button_press(GtkWidget *btn, GdkEventButton *event, GtkWidget *page)
{
	RemminaConnectionObject *cnnobj = rcw_cnnobj_of_page(page);
	const gchar *protocol = cnnobj && cnnobj->remmina_file ? remmina_file_get_string(cnnobj->remmina_file, "protocol") : NULL;
	GtkWidget *menu, *item;

	if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
		return FALSE;
	menu = gtk_menu_new();
	/* SSH and SFTP to the same server: one opens the other */
	if (g_strcmp0(protocol, "SSH") == 0 || g_strcmp0(protocol, "SFTP") == 0) {
		gboolean ssh = g_strcmp0(protocol, "SSH") == 0;
		item = gtk_menu_item_new_with_mnemonic(ssh ? _("Open _SFTP") : _("Open SSH _Terminal"));
		g_object_set_data(G_OBJECT(item), "protocol", (gpointer)(ssh ? "SFTP" : "SSH"));
		gtk_widget_set_sensitive(item, cnnobj->connected);
		g_signal_connect(item, "activate", G_CALLBACK(rcw_tab_menu_on_sibling), page);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	}
	item = gtk_menu_item_new_with_mnemonic(_("Move to New _Window"));
	gtk_widget_set_sensitive(item, rcw_page_can_move(page));
	g_signal_connect(item, "activate", G_CALLBACK(rcw_tab_menu_on_move_new_window), page);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_mnemonic(_("Window C_olour…"));
	g_signal_connect(item, "activate", G_CALLBACK(rcw_tab_menu_on_accent), btn);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_mnemonic(_("Rename _Session…"));
	g_signal_connect(item, "activate", G_CALLBACK(rcw_tab_menu_on_rename_session), btn);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	item = gtk_menu_item_new_with_mnemonic(_("_Close"));
	g_signal_connect(item, "activate", G_CALLBACK(rcw_tab_menu_on_close), page);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), btn, NULL);
	g_signal_connect(menu, "deactivate", G_CALLBACK(rcw_menu_on_deactivate), NULL);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	return TRUE;
}

/* A drop was taken by the tab strip of one of our windows: the tab moves there, no window opens */
static gboolean rcw_drag_accepted = FALSE;
/* The drag was cancelled (Escape): the tab stays */
static gboolean rcw_drag_cancelled = FALSE;

typedef struct {
	GtkWidget *			page;
	RemminaConnectionWindow *	to;	/* NULL: a new window at x, y */
	gint				x, y;
} RcwTabMove;

/* After the drag: moving the tab rebuilds the strips, which holds the drag source */
static gboolean rcw_tab_move_idle(gpointer data)
{
	RcwTabMove *m = data;

	if (gtk_widget_get_parent(m->page)) {
		if (!m->to)
			rcw_move_tab_to_new_window(m->page, TRUE, m->x, m->y);
		else if (m->to->priv)
			rcw_move_tab(m->page, m->to);
	}
	g_object_unref(m->page);
	if (m->to)
		g_object_unref(m->to);
	g_free(m);
	return G_SOURCE_REMOVE;
}

static void rcw_tab_move_later(GtkWidget *page, RemminaConnectionWindow *to, gint x, gint y)
{
	RcwTabMove *m = g_new0(RcwTabMove, 1);

	m->page = g_object_ref(page);
	m->to = to ? g_object_ref(to) : NULL;
	m->x = x;
	m->y = y;
	g_idle_add(rcw_tab_move_idle, m);
}

static void rcw_tab_on_drag_begin(GtkWidget *btn, GdkDragContext *context, GtkWidget *page)
{
	if (rcw_dragged_page)
		g_object_remove_weak_pointer(G_OBJECT(rcw_dragged_page), (gpointer *)&rcw_dragged_page);
	rcw_dragged_page = page;
	g_object_add_weak_pointer(G_OBJECT(page), (gpointer *)&rcw_dragged_page);
	rcw_drag_accepted = FALSE;
	rcw_drag_cancelled = FALSE;
}

static void rcw_tab_on_drag_data_get(GtkWidget *btn, GdkDragContext *context, GtkSelectionData *data, guint info,
				     guint time, GtkWidget *page)
{
	/* The page itself is in rcw_dragged_page: only our windows take this target */
	gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, (const guchar *)"tab", 3);
}

/* Dropped anywhere but on a tab strip of ours (outside the windows, on the desktop, on another program,
 * on the content of a window): the tab gets a window of its own, where it was dropped. Decided here
 * rather than from the result of the drag: under Plasma the application runs through XWayland and
 * KWin takes drops on the desktop and on Wayland programs itself, so such a drag does not end with
 * "no target" (it may even end as a success). */
static void rcw_tab_on_drag_end(GtkWidget *btn, GdkDragContext *context, GtkWidget *page)
{
	GtkWidget *dragged = rcw_dragged_page;
	GdkDevice *pointer;
	gint x = 0, y = 0;

	if (rcw_dragged_page)
		g_object_remove_weak_pointer(G_OBJECT(rcw_dragged_page), (gpointer *)&rcw_dragged_page);
	rcw_dragged_page = NULL;
	REMMINA_DEBUG("Tab drag ended: dropped on a tab strip %d, cancelled %d", rcw_drag_accepted, rcw_drag_cancelled);
	if (!dragged || rcw_drag_accepted || rcw_drag_cancelled || !rcw_page_can_move(dragged))
		return;
	pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(btn)));
	if (pointer)
		gdk_device_get_position(pointer, NULL, &x, &y);
	rcw_tab_move_later(dragged, NULL, x, y);
}

/* Only Escape keeps the tab where it is; the other failures are drops outside (see drag-end) */
static gboolean rcw_tab_on_drag_failed(GtkWidget *btn, GdkDragContext *context, GtkDragResult result, GtkWidget *page)
{
	REMMINA_DEBUG("Tab drag failed: result %d", result);
	if (result == GTK_DRAG_RESULT_USER_CANCELLED) {
		rcw_drag_cancelled = TRUE;
		return FALSE;	/* the icon flies back */
	}
	return TRUE;
}

/* A tab of another window dropped on this window's tab strip moves here */
static gboolean rcw_tabstrip_on_drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time,
					  RemminaConnectionWindow *cnnwin)
{
	GtkWidget *page = rcw_dragged_page;

	if (gtk_drag_dest_find_target(widget, context, NULL) == GDK_NONE || !page)
		return FALSE;
	rcw_drag_accepted = TRUE;
	gtk_drag_finish(context, TRUE, FALSE, time);
	rcw_tab_move_later(page, cnnwin, 0, 0);
	return TRUE;
}

/* One tab of the strip: a box with the toggle (icon, or icon + title) and, except for the
 * home tab, a separate close button. The toggle is kept in the box's "toggle" data. */
static GtkWidget *rcw_tabstrip_tab_new(RemminaConnectionWindow *cnnwin, GtkWidget *page, GSList **group)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *tab, *btn, *hbox, *w, *close;
	const gchar *icon = NULL, *title = NULL;
	gboolean home = (page == priv->home_page || g_object_get_data(G_OBJECT(page), "home-page") != NULL);

	tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(tab), "unidesk-tab-box");
	g_object_set_data(G_OBJECT(tab), "page", page);

	btn = gtk_radio_button_new(*group);
	*group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(btn));
	gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);
	gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(btn, FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "unidesk-tab");
	g_object_set_data(G_OBJECT(btn), "page", page);
	g_object_set_data(G_OBJECT(tab), "toggle", btn);
	g_signal_connect(G_OBJECT(btn), "toggled", G_CALLBACK(rcw_tabstrip_on_tab_toggled), cnnwin);
	gtk_box_pack_start(GTK_BOX(tab), btn, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_container_add(GTK_CONTAINER(btn), hbox);
	if (home) {
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "unidesk-tab-home");
		gtk_widget_set_tooltip_text(btn, _("Connections"));
		w = gtk_image_new_from_icon_name("go-home-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
		g_signal_connect(G_OBJECT(btn), "button-press-event", G_CALLBACK(rcw_home_tab_on_button_press), NULL);
		return tab;
	}

	cnnobj = g_object_get_data(G_OBJECT(page), "active-cnnobj");
	if (!cnnobj)
		cnnobj = g_object_get_data(G_OBJECT(page), "cnnobj");
	if (cnnobj && cnnobj->remmina_file) {
		icon = remmina_file_get_icon_name(cnnobj->remmina_file);
		title = remmina_file_get_string(cnnobj->remmina_file, "name");
	} else {
		icon = (const gchar *)g_object_get_data(G_OBJECT(page), "tool-icon");
		title = rcw_tool_page_title(page);
	}
	if (icon && icon[0]) {
		w = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
		gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	}
	w = rcw_tab_title_label_new(title);
	gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);

	close = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(close, FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(close), "unidesk-tab-close");
	gtk_widget_set_tooltip_text(close, _("Close tab (Meta+W)"));
	w = gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(close), w);
	g_signal_connect(G_OBJECT(close), "clicked", G_CALLBACK(rcw_tabstrip_on_close_clicked), page);
	gtk_box_pack_start(GTK_BOX(tab), close, FALSE, FALSE, 0);

	/* The tab menu, and dragging the tab to another window or out of the windows */
	g_signal_connect(G_OBJECT(btn), "button-press-event", G_CALLBACK(rcw_tabstrip_on_button_press), page);
	if (rcw_page_can_move(page)) {
		gtk_drag_source_set(btn, GDK_BUTTON1_MASK, rcw_tab_targets, G_N_ELEMENTS(rcw_tab_targets), GDK_ACTION_MOVE);
		if (icon && icon[0])
			gtk_drag_source_set_icon_name(btn, icon);
		g_signal_connect(G_OBJECT(btn), "drag-begin", G_CALLBACK(rcw_tab_on_drag_begin), page);
		g_signal_connect(G_OBJECT(btn), "drag-data-get", G_CALLBACK(rcw_tab_on_drag_data_get), page);
		g_signal_connect(G_OBJECT(btn), "drag-end", G_CALLBACK(rcw_tab_on_drag_end), page);
		g_signal_connect(G_OBJECT(btn), "drag-failed", G_CALLBACK(rcw_tab_on_drag_failed), page);
	}
	return tab;
}

/* Highlight of the current tab box */
static void rcw_tabstrip_tab_set_current(GtkWidget *tab, gboolean current)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context(tab);

	if (current)
		gtk_style_context_add_class(ctx, "current");
	else
		gtk_style_context_remove_class(ctx, "current");
}

/* Recreate the toggles from the notebook pages; the "+" button stays last */
static void rcw_tabstrip_rebuild(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkNotebook *notebook;
	GList *children, *l;
	GSList *group = NULL;
	GtkWidget *btn, *page;
	gint n, cur, i;

	if (!priv || !priv->tabstrip || !GTK_IS_WIDGET(priv->tabstrip) || !priv->notebook || !GTK_IS_NOTEBOOK(priv->notebook))
		return;
	notebook = GTK_NOTEBOOK(priv->notebook);
	priv->tabstrip_updating = TRUE;

	children = gtk_container_get_children(GTK_CONTAINER(priv->tabstrip));
	for (l = children; l; l = l->next)
		if (l->data != priv->tabstrip_new_button)
			gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(children);

	n = gtk_notebook_get_n_pages(notebook);
	cur = gtk_notebook_get_current_page(notebook);
	for (i = 0; i < n; i++) {
		GtkWidget *tab;

		page = gtk_notebook_get_nth_page(notebook, i);
		tab = rcw_tabstrip_tab_new(cnnwin, page, &group);
		btn = (GtkWidget *)g_object_get_data(G_OBJECT(tab), "toggle");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), i == cur);
		rcw_tabstrip_tab_set_current(tab, i == cur);
		gtk_box_pack_start(GTK_BOX(priv->tabstrip), tab, FALSE, FALSE, 0);
		gtk_widget_show_all(tab);
	}
	if (priv->tabstrip_new_button)
		gtk_box_reorder_child(GTK_BOX(priv->tabstrip), priv->tabstrip_new_button, -1);
	priv->tabstrip_updating = FALSE;
}

/* Mirror the current notebook page in the strip and keep its toggle in view.
 * page is the page becoming current (from "switch-page"), or NULL to use the notebook's current page. */
static void rcw_tabstrip_sync_current(RemminaConnectionWindow *cnnwin, GtkWidget *page)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GList *children, *l;
	gint cur;

	if (!priv || !priv->tabstrip || !GTK_IS_WIDGET(priv->tabstrip) || !priv->notebook || !GTK_IS_NOTEBOOK(priv->notebook))
		return;
	if (!page) {
		cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook));
		page = cur >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(priv->notebook), cur) : NULL;
	}
	priv->tabstrip_updating = TRUE;
	children = gtk_container_get_children(GTK_CONTAINER(priv->tabstrip));
	for (l = children; l; l = l->next) {
		GtkWidget *tab = GTK_WIDGET(l->data);
		GtkWidget *btn = (GtkWidget *)g_object_get_data(G_OBJECT(tab), "toggle");

		if (!btn)
			continue;	/* the "+" button */
		if (g_object_get_data(G_OBJECT(tab), "page") != page) {
			rcw_tabstrip_tab_set_current(tab, FALSE);
			continue;
		}
		rcw_tabstrip_tab_set_current(tab, TRUE);
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)))
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
		if (priv->tabstrip_scroller && gtk_widget_get_realized(tab)) {
			GtkAllocation a;
			GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(priv->tabstrip_scroller));

			gtk_widget_get_allocation(tab, &a);
			gtk_adjustment_clamp_page(adj, a.x, a.x + a.width);
		}
	}
	g_list_free(children);
	priv->tabstrip_updating = FALSE;
}

/* The strip itself: a horizontally scrollable box of tabs */
static GtkWidget *rcw_tabstrip_new(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *sw, *box, *button, *image = NULL, *vp;

	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);
#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), TRUE);
#endif
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 120);
	gtk_widget_set_hexpand(sw, TRUE);
	gtk_widget_set_halign(sw, GTK_ALIGN_FILL);
	gtk_widget_set_valign(sw, GTK_ALIGN_FILL);
	gtk_style_context_add_class(gtk_widget_get_style_context(sw), "unidesk-tabstrip");

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(box, GTK_ALIGN_FILL);

	/* The "new connection" button lives in the toolbar of the home tab (remmina_main.c), not here */
	button = NULL;
	(void)image;

	gtk_container_add(GTK_CONTAINER(sw), box);
	vp = gtk_bin_get_child(GTK_BIN(sw));
	if (GTK_IS_VIEWPORT(vp))
		gtk_viewport_set_shadow_type(GTK_VIEWPORT(vp), GTK_SHADOW_NONE);
	gtk_widget_show_all(sw);

	/* Tabs of other windows can be dropped here */
	gtk_drag_dest_set(sw, GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT, rcw_tab_targets,
			  G_N_ELEMENTS(rcw_tab_targets), GDK_ACTION_MOVE);
	g_signal_connect(sw, "drag-drop", G_CALLBACK(rcw_tabstrip_on_drag_drop), cnnwin);

	priv->tabstrip_scroller = sw;
	priv->tabstrip = box;
	priv->tabstrip_new_button = button;
	return sw;
}

GtkWidget *rcw_main_add_window_as_tab(GtkWindow *win, const gchar *title, const gchar *icon_name)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	GtkWidget *child, *page, *label;
	const gchar *t;
	gint n;

	g_return_val_if_fail(GTK_IS_WINDOW(win), NULL);

	/* Already hosted (e.g. a second request for the same preferences window): just switch to it */
	page = (GtkWidget *)g_object_get_data(G_OBJECT(win), "tool-page");
	if (page && GTK_IS_WIDGET(page)) {
		GtkWidget *parent = gtk_widget_get_parent(page);
		if (parent && GTK_IS_NOTEBOOK(parent))
			gtk_notebook_set_current_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), page));
		rcw_main_present();
		return page;
	}

	cnnwin = rcw_get_main();

	t = title;
	if (!t || !t[0])
		t = gtk_window_get_title(win);
	if (!t || !t[0])
		t = "UniDeskBrowser";

	child = gtk_bin_get_child(GTK_BIN(win));
	g_return_val_if_fail(child != NULL, NULL);

	g_object_ref(child);
	gtk_container_remove(GTK_CONTAINER(win), child);

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(page, "unidesk-tool-page");
	gtk_box_pack_start(GTK_BOX(page), child, TRUE, TRUE, 0);
	gtk_widget_show(child);
	g_object_unref(child);

	g_object_set_data(G_OBJECT(page), "tool-window", win);
	g_object_set_data_full(G_OBJECT(page), "tool-title", g_strdup(t), g_free);
	g_object_set_data_full(G_OBJECT(page), "tool-icon", g_strdup(icon_name ? icon_name : ""), g_free);
	g_object_set_data(G_OBJECT(win), "tool-page", page);
	g_signal_connect(G_OBJECT(page), "destroy", G_CALLBACK(rcw_tool_page_destroyed), win);
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(rcw_tool_window_destroyed), NULL);

	label = rcw_create_tool_tab_label(t, icon_name, GTK_WIDGET(win));
	n = gtk_notebook_append_page(cnnwin->priv->notebook, page, label);
	gtk_notebook_set_tab_reorderable(cnnwin->priv->notebook, page, TRUE);
	gtk_widget_set_can_focus(gtk_widget_get_parent(label), FALSE);
	gtk_widget_show(page);

	/* The window itself is never shown: our window is the only one */
	rcw_main_present();
	gtk_notebook_set_current_page(cnnwin->priv->notebook, n);
	rcw_update_empty_state(cnnwin);

	return page;
}

static GtkWidget *rcw_create_empty_state(RemminaConnectionWindow *cnnwin)
{
	GtkWidget *box, *image, *label, *button;
	gchar *markup;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_name(box, "unidesk-empty-state");
	gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

	image = gtk_image_new_from_icon_name("network-server-symbolic", GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(image), 96);
	gtk_style_context_add_class(gtk_widget_get_style_context(image), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	markup = g_markup_printf_escaped("<span size=\"x-large\" weight=\"bold\">%s</span>", _("No open connections"));
	gtk_label_set_markup(GTK_LABEL(label), markup);
	g_free(markup);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	label = gtk_label_new(_("Pick a connection in the sidebar or create a new one"));
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 40);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	button = gtk_button_new_with_label(_("New connection"));
	gtk_actionable_set_action_name(GTK_ACTIONABLE(button), "main.new");
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "suggested-action");
	gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(button, 12);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);

	gtk_widget_show_all(box);
	return box;
}

/* Placeholder vs tabs, toolbar visibility and window title of the application window */
static void rcw_update_empty_state(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *cur = NULL;
	gboolean tool;
	gint n;

	if (!priv || !priv->content_stack || !priv->notebook)
		return;
	n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook));
	gtk_stack_set_visible_child_name(GTK_STACK(priv->content_stack), n > 0 ? "tabs" : "empty");
	if (n > 0)
		cur = gtk_notebook_get_nth_page(GTK_NOTEBOOK(priv->notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)));
	tool = rcw_page_is_tool(cur);
	if (priv->toolbar)
		gtk_widget_set_visible(priv->toolbar, n > 0 && !tool && !remmina_pref.hide_connection_toolbar);
	/* The header bar button mirrors the toolbar preference (changed by the
	 * preferences tab or the keyboard shortcut too) */
	if (priv->sidebar_toggle && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->sidebar_toggle)) == remmina_pref.hide_connection_toolbar) {
		g_signal_handlers_block_by_func(priv->sidebar_toggle, rcw_on_sidebar_toggle, cnnwin);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->sidebar_toggle), !remmina_pref.hide_connection_toolbar);
		g_signal_handlers_unblock_by_func(priv->sidebar_toggle, rcw_on_sidebar_toggle, cnnwin);
	}
	if (n == 0 || cur == priv->home_page) {
		gtk_window_set_title(GTK_WINDOW(cnnwin), "UniDeskBrowser");
	} else if (tool) {
		gchar *title = g_strdup_printf("%s — UniDeskBrowser", rcw_tool_page_title(cur));
		gtk_window_set_title(GTK_WINDOW(cnnwin), title);
		g_free(title);
	}
}

static gboolean rcw_on_switch_tool_page_idle(gpointer user_data)
{
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)user_data;

	if (cnnwin && cnnwin->priv) {
		cnnwin->priv->spf_eventsourceid = 0;
		rcw_update_empty_state(cnnwin);
	}
	return G_SOURCE_REMOVE;
}

static RemminaConnectionWindow *rcw_find(RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
	/* Every connection opens as a tab of the current window (or of the window a restored tab
	 * belongs to), or of the fullscreen window holding its tabs */
	RemminaConnectionWindow *cnnwin = rcw_open_target && rcw_open_target->priv ? rcw_open_target : rcw_get_main();
	RemminaConnectionWindow *fs = cnnwin->priv ? cnnwin->priv->fullscreen_win : NULL;

	if (fs && REMMINA_IS_CONNECTION_WINDOW(fs) && fs->priv)
		return fs;
	return cnnwin;
}

static gboolean rcw_delayed_window_present(gpointer user_data)
{
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)user_data;

	if (cnnwin) {
		gtk_window_present_with_time(GTK_WINDOW(cnnwin), (guint32)(g_get_monotonic_time() / 1000));
		rcw_grab_focus(cnnwin);
	}
	cnnwin->priv->dwp_eventsourceid = 0;
	return G_SOURCE_REMOVE;
}

static void rco_on_connect(RemminaProtocolWidget *gp, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

	REMMINA_DEBUG("Connect signal emitted");

	/* This signal handler is called by a plugin when it’s correctly connected
	 * (and authenticated) */

	cnnobj->connected = TRUE;

	remmina_protocol_widget_set_hostkey_func(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
						 (RemminaHostkeyFunc)rcw_hostkey_func);

	/** Remember recent list for quick connect, and save the current date
	 * in the last_used field.
	 */
	if (remmina_file_get_filename(cnnobj->remmina_file) == NULL)
		remmina_pref_add_recent(remmina_file_get_string(cnnobj->remmina_file, "protocol"),
					remmina_file_get_string(cnnobj->remmina_file, "server"));
	REMMINA_DEBUG("We save the last successful connection date");
	remmina_file_state_last_success(cnnobj->remmina_file);

	REMMINA_DEBUG("Saving credentials");
	/* Save credentials */
	remmina_file_save(cnnobj->remmina_file);

	if (cnnobj->cnnwin->priv->floating_toolbar_widget)
		gtk_widget_show(cnnobj->cnnwin->priv->floating_toolbar_widget);

	rco_update_toolbar(cnnobj);

	rcw_keyboard_grab_request(cnnobj, TRUE);

	REMMINA_DEBUG("Trying to present the window");
	/* Try to present window */
	cnnobj->cnnwin->priv->dwp_eventsourceid = g_timeout_add(200, rcw_delayed_window_present, (gpointer)cnnobj->cnnwin);
	rcw_show_widget_areas(cnnobj->cnnwin);
}

static void cb_lasterror_confirmed(void *cbdata, int btn)
{
	TRACE_CALL(__func__);
	rco_closewin((RemminaProtocolWidget *)cbdata);
}

static void rco_on_disconnect(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	if (!gp || !gp->cnnobj)
		return;
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	if (!cnnobj->cnnwin || !cnnobj->cnnwin->priv)
		return;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkWidget *pparent;
	gboolean is_visible_tab;

	REMMINA_DEBUG("Disconnect signal received on RemminaProtocolWidget");
	/* Detach the protocol widget from the notebook now, or we risk that a
	 * window delete will destroy cnnobj->proto before we complete disconnection.
	 */
	pparent = gtk_widget_get_parent(cnnobj->proto);
	if (pparent != NULL) {
		g_object_ref(cnnobj->proto);
		gtk_container_remove(GTK_CONTAINER(pparent), cnnobj->proto);
	}

	is_visible_tab = (rcw_get_visible_cnnobj(cnnobj->cnnwin) == cnnobj);

	if (is_visible_tab) {
		rcw_kp_ungrab(cnnobj->cnnwin);
	}
	
	cnnobj->connected = FALSE;

	if (remmina_pref.save_view_mode) {
		if (cnnobj->cnnwin)
			remmina_file_set_int(cnnobj->remmina_file, "viewmode", cnnobj->cnnwin->priv->view_mode);
		remmina_file_save(cnnobj->remmina_file);
	}

	if (is_visible_tab && priv->toolitem_grab) {
		gtk_toggle_tool_button_set_active(
			GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_grab),
			FALSE);
	}

	if (remmina_protocol_widget_has_error(gp)) {
		/* We cannot close window immediately, but we must show a message panel */
		RemminaMessagePanel *mp;
		/* Destroy scrolled_container (and viewport) and all its children the plugin created
		 * on it, so they will not receive GUI signals */
		if (cnnobj->scrolled_container) {
			gtk_widget_destroy(cnnobj->scrolled_container);
			cnnobj->scrolled_container = NULL;
		}
		cnnobj->viewport = NULL;
		mp = remmina_message_panel_new();
		remmina_message_panel_setup_message(mp, remmina_protocol_widget_get_error_message(gp), cb_lasterror_confirmed, gp);
		rco_show_message_panel(gp->cnnobj, mp);
		REMMINA_DEBUG("Could not disconnect");
	} else {
		rco_closewin(gp);
		REMMINA_DEBUG("Disconnected");
	}
}

static void rco_on_desktop_resize(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	if (cnnobj && cnnobj->cnnwin && cnnobj->cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		rco_check_resize(cnnobj);
}

static void rco_on_update_align(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	remmina_protocol_widget_update_alignment(cnnobj);
}

static void rco_on_lock_dynres(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	cnnobj->dynres_unlocked = FALSE;
	rco_update_toolbar(cnnobj);
}

static void rco_on_unlock_dynres(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	cnnobj->dynres_unlocked = TRUE;
	rco_update_toolbar(cnnobj);
}

gboolean rcw_open_from_filename(const gchar *filename)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile;
	GtkWidget *dialog;

	remminafile = remmina_file_manager_load_file(filename);
	if (remminafile) {
		if (remmina_file_get_int (remminafile, "profile-lock", FALSE)
				&& remmina_unlock_new(remmina_main_get_window()) == 0)
			return FALSE;
		rcw_open_from_file(remminafile);
		return TRUE;
	} else {
		dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						_("The file “%s” is corrupted, unreadable, or could not be found."), filename);
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dialog);
		remmina_widget_pool_register(dialog);
		return FALSE;
	}
}

static gboolean open_connection_last_stage(gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;

	/* Now we have an allocated size for our RemminaProtocolWidget. We can proceed with the connection */
	remmina_protocol_widget_update_remote_resolution(gp);
	remmina_protocol_widget_open_connection(gp);
	rco_check_resize(gp->cnnobj);

	return FALSE;
}

static void rpw_size_allocated_on_connection(GtkWidget *w, GdkRectangle *allocation, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)w;

	/* Disconnect signal handler to avoid to be called again after a normal resize */
	g_signal_handler_disconnect(w, gp->cnnobj->deferred_open_size_allocate_handler);

	/* Allow extra 100 ms for size allocation (do we really need it?) */
	g_timeout_add(100, open_connection_last_stage, gp);

	return;
}

void rcw_open_from_file(RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
	rcw_open_from_file_full(remminafile, NULL, NULL, NULL);
}

static void set_label_selectable(gpointer data, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkWidget *widget = GTK_WIDGET(data);

	if (GTK_IS_LABEL(widget))
		gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
}

/**
 * @brief These define the response id's of the
 *	  gtksocket-is-not-available-warning-dialog buttons.
 */
enum GTKSOCKET_NOT_AVAIL_RESPONSE_TYPE {
	GTKSOCKET_NOT_AVAIL_RESPONSE_OPEN_BROWSER = 0,
	GTKSOCKET_NOT_AVAIL_RESPONSE_NUM
};

/**
 * @brief Gets called if the user interacts with the
 *	  gtksocket-is-not-available-warning-dialog
 */
static void rcw_gtksocket_not_available_dialog_response(GtkDialog *			self,
							gint				response_id,
							RemminaConnectionObject *	cnnobj)
{
	TRACE_CALL(__func__);

	// Close the current page since it's useless without GtkSocket.
	// The user would need to manually click the close button.
	if (cnnobj) rco_disconnect_current_page(cnnobj);

	gtk_widget_destroy(GTK_WIDGET(self));
}

GtkWidget *rcw_open_from_file_full(RemminaFile *remminafile, GCallback disconnect_cb, gpointer data, guint *handler)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	gint ret;
	GtkWidget *dialog;
	GtkWidget *newpage;
	gint view_mode;
	const gchar *msg;
	RemminaScaleMode scalemode;

	if (disconnect_cb) {
		g_print("disconnect_cb is deprecated inside rcw_open_from_file_full() and should be null\n");
		return NULL;
	}

	if (!remminafile) {
		return NULL;
	}


	/* Create the RemminaConnectionObject */
	cnnobj = g_new0(RemminaConnectionObject, 1);
	cnnobj->remmina_file = remminafile;

	/* Create the RemminaProtocolWidget */
	cnnobj->proto = remmina_protocol_widget_new();
	remmina_protocol_widget_setup((RemminaProtocolWidget *)cnnobj->proto, remminafile, cnnobj);
	if (remmina_protocol_widget_has_error((RemminaProtocolWidget *)cnnobj->proto)) {
		GtkWindow *wparent;
		wparent = remmina_main_get_window();
		msg = remmina_protocol_widget_get_error_message((RemminaProtocolWidget *)cnnobj->proto);
		dialog = gtk_message_dialog_new(wparent, GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		/* We should destroy cnnobj->proto and cnnobj now… TODO: Fix this leak */
		return NULL;
	}



	/* Set a name for the widget, for CSS selector */
	gtk_widget_set_name(GTK_WIDGET(cnnobj->proto), "remmina-protocol-widget");

	gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
	gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);

	if (data)
		g_object_set_data(G_OBJECT(cnnobj->proto), "user-data", data);

	view_mode = remmina_file_get_int(cnnobj->remmina_file, "viewmode", 0);
	gint ismultimon = remmina_file_get_int(cnnobj->remmina_file, "multimon", 0);

	if (ismultimon)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	if (fullscreen)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	if (remmina_pref.start_fullscreen){
		view_mode = VIEWPORT_FULLSCREEN_MODE;
	}

	/* Create the viewport to make the RemminaProtocolWidget scrollable */
	cnnobj->viewport = gtk_viewport_new(NULL, NULL);
	gtk_widget_set_name(cnnobj->viewport, "remmina-cw-viewport");
	gtk_widget_show(cnnobj->viewport);
	gtk_container_set_border_width(GTK_CONTAINER(cnnobj->viewport), 0);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(cnnobj->viewport), GTK_SHADOW_NONE);

	/* The connection becomes a tab of the application window (or of the active fullscreen window) */
	cnnobj->cnnwin = rcw_find(remminafile);

	/* Create the scrolled container for the window mode it will live in */
	scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
	cnnobj->scrolled_container = rco_create_scrolled_container(scalemode, cnnobj->cnnwin->priv->view_mode);
	remmina_protocol_widget_set_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), scalemode);

	gtk_container_add(GTK_CONTAINER(cnnobj->scrolled_container), cnnobj->viewport);

	/* Determine whether the plugin can scale or not. If the plugin can scale and we do
	 * not want to expand, then we add a GtkAspectFrame to maintain aspect ratio during scaling */
	cnnobj->plugin_can_scale = remmina_plugin_manager_query_feature_by_type(REMMINA_PLUGIN_TYPE_PROTOCOL,
										remmina_file_get_string(remminafile, "protocol"),
										REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

	cnnobj->aspectframe = NULL;
	gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);

	newpage = rcw_append_new_page(cnnobj->cnnwin, cnnobj);
	gtk_widget_show(GTK_WIDGET(cnnobj->cnnwin));
	gtk_window_present(GTK_WINDOW(cnnobj->cnnwin));
	nb_set_current_page(cnnobj->cnnwin->priv->notebook, newpage);

	/* A profile that wants fullscreen moves the tabs into a fullscreen window */
	if ((view_mode == VIEWPORT_FULLSCREEN_MODE || view_mode == SCROLLED_FULLSCREEN_MODE) &&
	    cnnobj->cnnwin->priv->view_mode == SCROLLED_WINDOW_MODE) {
		cnnobj->cnnwin->priv->fss_view_mode = view_mode;
		rcw_switch_viewmode(cnnobj->cnnwin, view_mode);
	}
	if (view_mode == VIEWPORT_FULLSCREEN_MODE && ismultimon) {
	        rcw_create_widget_areas(cnnobj);
	}

	// Do not call remmina_protocol_widget_update_alignment(cnnobj); here or cnnobj->proto will not fill its parent size
	// and remmina_protocol_widget_update_remote_resolution() cannot autodetect available space

	gtk_widget_show(cnnobj->proto);
	g_signal_connect(G_OBJECT(cnnobj->proto), "connect", G_CALLBACK(rco_on_connect), cnnobj);
	g_signal_connect(G_OBJECT(cnnobj->proto), "disconnect", G_CALLBACK(rco_on_disconnect), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "desktop-resize", G_CALLBACK(rco_on_desktop_resize), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "update-align", G_CALLBACK(rco_on_update_align), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "lock-dynres", G_CALLBACK(rco_on_lock_dynres), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "unlock-dynres", G_CALLBACK(rco_on_unlock_dynres), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "enter-notify-event", G_CALLBACK(rco_enter_protocol_widget), cnnobj);
	g_signal_connect(G_OBJECT(cnnobj->proto), "leave-notify-event", G_CALLBACK(rco_leave_protocol_widget), cnnobj);

	if (!remmina_pref.save_view_mode)
		remmina_file_set_int(cnnobj->remmina_file, "viewmode", remmina_pref.default_mode);


	/* If it is a GtkSocket plugin and X11 is not available, we inform the
	 * user and close the connection */
	ret = remmina_plugin_manager_query_feature_by_type(REMMINA_PLUGIN_TYPE_PROTOCOL,
							   remmina_file_get_string(remminafile, "protocol"),
							   REMMINA_PROTOCOL_FEATURE_TYPE_GTKSOCKET);
	if (ret && !remmina_gtksocket_available()) {
		gchar *title = _("Warning: This plugin requires GtkSocket, but this "
				 "feature is unavailable in a Wayland session.");

		gchar *err_msg =
			_("Plugins relying on GtkSocket can't run in a "
			  "Wayland session.\nStart the application in an X11 session "
			  "(or with GDK_BACKEND=x11) to use this plugin.");

		dialog = gtk_message_dialog_new(
			GTK_WINDOW(cnnobj->cnnwin),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_OK,
			"%s", title);

		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
							 err_msg);
		REMMINA_CRITICAL(g_strdup_printf("%s\n%s", title, err_msg));

		g_signal_connect(G_OBJECT(dialog),
				 "response",
				 G_CALLBACK(rcw_gtksocket_not_available_dialog_response),
				 cnnobj);

		// Make Text selectable. Usefull because of the link in the text.
		GtkWidget *area = gtk_message_dialog_get_message_area(
			GTK_MESSAGE_DIALOG(dialog));
		GtkContainer *box = (GtkContainer *)area;

		GList *children = gtk_container_get_children(box);
		g_list_foreach(children, set_label_selectable, NULL);
		g_list_free(children);

		gtk_widget_show(dialog);

		return NULL;    /* Should we destroy something before returning? */
	}

	if (cnnobj->cnnwin->priv->floating_toolbar_widget)
		gtk_widget_show(cnnobj->cnnwin->priv->floating_toolbar_widget);

	if (remmina_protocol_widget_has_error((RemminaProtocolWidget *)cnnobj->proto)) {
		printf("OK, an error occurred in initializing the protocol plugin before connecting. The error is %s.\n"
		       "TODO: Put this string as an error to show", remmina_protocol_widget_get_error_message((RemminaProtocolWidget *)cnnobj->proto));
		return cnnobj->proto;
	}


	/* GTK window setup is done here, and we are almost ready to call remmina_protocol_widget_open_connection().
	 * But size has not yet been allocated by GTK
	 * to the widgets. If we are in RES_USE_INITIAL_WINDOW_SIZE resolution mode or scale is REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES,
	 * we should wait for a size allocation from GTK for cnnobj->proto
	 * before connecting */

	cnnobj->deferred_open_size_allocate_handler = g_signal_connect(G_OBJECT(cnnobj->proto), "size-allocate", G_CALLBACK(rpw_size_allocated_on_connection), NULL);

	return cnnobj->proto;
}

static void on_paned_initial_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
	GtkOrientation orient = GPOINTER_TO_INT(user_data);
	gint size = (orient == GTK_ORIENTATION_VERTICAL) ? allocation->height : allocation->width;
	if (size > 0) {
		gtk_paned_set_position(GTK_PANED(widget), size / 2);
	}
	g_signal_handlers_disconnect_by_func(widget, on_paned_initial_size_allocate, user_data);
}

void rcw_split_connection_full(RemminaProtocolWidget *gp, GtkOrientation orientation, gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *source_cnnobj;
	RemminaConnectionObject *new_cnnobj;
	RemminaConnectionWindow *cnnwin;
	GtkWidget *page;
	GtkWidget *source_widget;
	GtkWidget *parent;
	GtkWidget *paned;
	GtkAllocation alloc;
	gint pos;
	RemminaScaleMode scalemode;
	gint view_mode;

	if (!gp || !gp->cnnobj)
		return;

	source_cnnobj = gp->cnnobj;
	cnnwin = source_cnnobj->cnnwin;
	if (!cnnwin || !cnnwin->priv || !cnnwin->priv->notebook)
		return;

	page = source_cnnobj->tab_page ? source_cnnobj->tab_page : nb_find_page_by_cnnobj(cnnwin->priv->notebook, source_cnnobj);
	if (!page)
		return;

	source_widget = source_cnnobj->scrolled_container;
	if (!source_widget)
		return;

	parent = gtk_widget_get_parent(source_widget);
	if (!parent)
		return;

	new_cnnobj = g_new0(RemminaConnectionObject, 1);
	new_cnnobj->cnnwin = cnnwin;
	new_cnnobj->tab_page = page;
	new_cnnobj->remmina_file = remmina_file_dup(source_cnnobj->remmina_file);

	new_cnnobj->proto = remmina_protocol_widget_new();
	remmina_protocol_widget_setup(REMMINA_PROTOCOL_WIDGET(new_cnnobj->proto), new_cnnobj->remmina_file, new_cnnobj);

	if (remmina_protocol_widget_has_error(REMMINA_PROTOCOL_WIDGET(new_cnnobj->proto))) {
		REMMINA_WARNING("Failed to setup protocol widget for split connection");
		g_free(new_cnnobj);
		return;
	}

	gtk_widget_set_name(GTK_WIDGET(new_cnnobj->proto), "remmina-protocol-widget");
	gtk_widget_set_halign(GTK_WIDGET(new_cnnobj->proto), GTK_ALIGN_FILL);
	gtk_widget_set_valign(GTK_WIDGET(new_cnnobj->proto), GTK_ALIGN_FILL);

	if (user_data)
		g_object_set_data(G_OBJECT(new_cnnobj->proto), "user-data", user_data);

	/* Viewport */
	new_cnnobj->viewport = gtk_viewport_new(NULL, NULL);
	gtk_widget_set_name(new_cnnobj->viewport, "remmina-cw-viewport");
	gtk_widget_show(new_cnnobj->viewport);
	gtk_container_set_border_width(GTK_CONTAINER(new_cnnobj->viewport), 0);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(new_cnnobj->viewport), GTK_SHADOW_NONE);

	view_mode = SCROLLED_WINDOW_MODE;
	scalemode = get_current_allowed_scale_mode(new_cnnobj, NULL, NULL);
	new_cnnobj->scrolled_container = rco_create_scrolled_container(scalemode, view_mode);
	remmina_protocol_widget_set_current_scale_mode(REMMINA_PROTOCOL_WIDGET(new_cnnobj->proto), scalemode);

	gtk_container_add(GTK_CONTAINER(new_cnnobj->scrolled_container), new_cnnobj->viewport);
	new_cnnobj->plugin_can_scale = remmina_plugin_manager_query_feature_by_type(
		REMMINA_PLUGIN_TYPE_PROTOCOL,
		remmina_file_get_string(new_cnnobj->remmina_file, "protocol"),
		REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

	new_cnnobj->aspectframe = NULL;
	gtk_container_add(GTK_CONTAINER(new_cnnobj->viewport), new_cnnobj->proto);

	paned = gtk_paned_new(orientation);
	gtk_widget_show(paned);

	// Get current size of source widget to split 50/50 
	gtk_widget_get_allocation(source_widget, &alloc);
	pos = (orientation == GTK_ORIENTATION_VERTICAL) ? alloc.height / 2 : alloc.width / 2;

	// Replace source_widget in parent with paned 
	g_object_ref(source_widget);
	if (GTK_IS_BOX(parent)) {
		gtk_container_remove(GTK_CONTAINER(parent), source_widget);
		gtk_box_pack_start(GTK_BOX(parent), paned, TRUE, TRUE, 0);
	} else if (GTK_IS_PANED(parent)) {
		if (gtk_paned_get_child1(GTK_PANED(parent)) == source_widget) {
			gtk_container_remove(GTK_CONTAINER(parent), source_widget);
			gtk_paned_pack1(GTK_PANED(parent), paned, TRUE, FALSE);
		} else {
			gtk_container_remove(GTK_CONTAINER(parent), source_widget);
			gtk_paned_pack2(GTK_PANED(parent), paned, TRUE, FALSE);
		}
	}

	gtk_paned_pack1(GTK_PANED(paned), source_widget, TRUE, FALSE);
	g_object_unref(source_widget);

	gtk_paned_pack2(GTK_PANED(paned), new_cnnobj->scrolled_container, TRUE, FALSE);
	if (pos > 0) {
		gtk_paned_set_position(GTK_PANED(paned), pos);
	} else {
		g_signal_connect(G_OBJECT(paned), "size-allocate", G_CALLBACK(on_paned_initial_size_allocate), GINT_TO_POINTER(orientation));
	}

	GList *list = g_object_get_data(G_OBJECT(page), "cnnobj-list");
	if (!list)
		list = g_list_append(NULL, source_cnnobj);
	list = g_list_append(list, new_cnnobj);
	g_object_set_data(G_OBJECT(page), "cnnobj-list", list);

	gtk_widget_show(new_cnnobj->proto);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "connect", G_CALLBACK(rco_on_connect), new_cnnobj);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "disconnect", G_CALLBACK(rco_on_disconnect), NULL);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "desktop-resize", G_CALLBACK(rco_on_desktop_resize), NULL);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "update-align", G_CALLBACK(rco_on_update_align), NULL);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "lock-dynres", G_CALLBACK(rco_on_lock_dynres), NULL);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "unlock-dynres", G_CALLBACK(rco_on_unlock_dynres), NULL);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "enter-notify-event", G_CALLBACK(rco_enter_protocol_widget), new_cnnobj);
	g_signal_connect(G_OBJECT(new_cnnobj->proto), "leave-notify-event", G_CALLBACK(rco_leave_protocol_widget), new_cnnobj);

	gtk_widget_show_all(paned);

	rcw_set_active_pane(cnnwin, new_cnnobj);

	new_cnnobj->deferred_open_size_allocate_handler = g_signal_connect(
		G_OBJECT(new_cnnobj->proto), "size-allocate",
		G_CALLBACK(rpw_size_allocated_on_connection), NULL);
}

void rcw_split_connection(RemminaProtocolWidget *gp, GtkOrientation orientation)
{
	rcw_split_connection_full(gp, orientation, NULL);
}

GtkWindow *rcw_get_gtkwindow(RemminaConnectionObject *cnnobj)
{
	return &cnnobj->cnnwin->window;
}
GtkWidget *rcw_get_gtkviewport(RemminaConnectionObject *cnnobj)
{
	return cnnobj->viewport;
}

void rcw_set_delete_confirm_mode(RemminaConnectionWindow *cnnwin, RemminaConnectionWindowOnDeleteConfirmMode mode)
{
	TRACE_CALL(__func__);
	cnnwin->priv->on_delete_confirm_mode = mode;
}

/**
 * Deletes a RemminaMessagePanel from the current cnnobj
 * and if it was visible, make visible the last remaining one.
 */
void rco_destroy_message_panel(RemminaConnectionObject *cnnobj, RemminaMessagePanel *mp)
{
	TRACE_CALL(__func__);
	GList *childs, *cc;
	RemminaMessagePanel *lastPanel;
	gboolean was_visible;
	GtkWidget *page;

	page = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
	childs = gtk_container_get_children(GTK_CONTAINER(page));
	cc = g_list_first(childs);
	while (cc != NULL) {
		if ((RemminaMessagePanel *)cc->data == mp)
			break;
		cc = g_list_next(cc);
	}
	g_list_free(childs);

	if (cc == NULL) {
		printf("Remmina: Warning. There was a request to destroy a RemminaMessagePanel that is not on the page\n");
		return;
	}
	was_visible = gtk_widget_is_visible(GTK_WIDGET(mp));
	gtk_widget_destroy(GTK_WIDGET(mp));

	/* And now, show the last remaining message panel, if needed */
	if (was_visible) {
		childs = gtk_container_get_children(GTK_CONTAINER(page));
		cc = g_list_first(childs);
		lastPanel = NULL;
		while (cc != NULL) {
			if (G_TYPE_CHECK_INSTANCE_TYPE(cc->data, REMMINA_TYPE_MESSAGE_PANEL))
				lastPanel = (RemminaMessagePanel *)cc->data;
			cc = g_list_next(cc);
		}
		g_list_free(childs);
		if (lastPanel)
			gtk_widget_show(GTK_WIDGET(lastPanel));
		else if (cnnobj->connected && cnnobj->proto)
			remmina_protocol_widget_grab_focus(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}
}

/**
 * Each cnnobj->page can have more than one RemminaMessagePanel, but 0 or 1 are visible.
 *
 * This function adds a RemminaMessagePanel to cnnobj->page, move it to top,
 * and makes it the only visible one.
 */
void rco_show_message_panel(RemminaConnectionObject *cnnobj, RemminaMessagePanel *mp)
{
	TRACE_CALL(__func__);
	GList *childs, *cc;
	GtkWidget *page;

	/* Hides all RemminaMessagePanels childs of cnnobj->page */
	page = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
	childs = gtk_container_get_children(GTK_CONTAINER(page));
	cc = g_list_first(childs);
	while (cc != NULL) {
		if (G_TYPE_CHECK_INSTANCE_TYPE(cc->data, REMMINA_TYPE_MESSAGE_PANEL))
			gtk_widget_hide(GTK_WIDGET(cc->data));
		cc = g_list_next(cc);
	}
	g_list_free(childs);

	if (remmina_pref.mp_left){
		gtk_widget_set_halign(GTK_WIDGET(mp), GTK_ALIGN_START);
	}

	/* Add the new message panel at the top of cnnobj->page */
	gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(mp), FALSE, FALSE, 0);
	gtk_box_reorder_child(GTK_BOX(page), GTK_WIDGET(mp), 0);

	/* Show the message panel */
	gtk_widget_show_all(GTK_WIDGET(mp));

	/* Focus the correct field of the RemminaMessagePanel */
	remmina_message_panel_focus_auth_entry(mp);
}

static gboolean rcw_map_event_fullscreen_monitor(GtkWidget *widget, GdkEvent *event,
	                                     gpointer data) {
	TRACE_CALL(__func__);
	MultimonWindow* w = RCW_MULTIMON(widget);
	GdkWindow*me = gtk_widget_get_window(GTK_WIDGET(widget));
	if (me == NULL || w->priv == NULL)
		return TRUE;
	gdk_window_set_fullscreen_mode(me,
									GDK_FULLSCREEN_ON_CURRENT_MONITOR);
	gdk_window_fullscreen_on_monitor(me, w->priv->monitor);
	gdk_window_set_keep_above(me, TRUE);
	gdk_window_set_accept_focus(me, TRUE);

	return TRUE;
}

/*
 *  Multimonitor extra windows management
 */


/* Focusing management */
static gboolean rcw_other_fs_enter(MultimonWindow*w, GdkEventCrossing *event, RemminaConnectionObject* cnnobj) {
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING");
	print_crossing_event(event);
#endif
	if (event->mode == GDK_CROSSING_UNGRAB) {
		// Someone steal our grab, take note and do not attempt to regrab
		rcw_keyboard_grab_request(cnnobj, FALSE);
		return FALSE;
	}
	rcw_keyboard_grab_request(cnnobj, TRUE);
	return TRUE;
}

static gboolean rcw_other_fs_leave(MultimonWindow*w, GdkEventCrossing *event, RemminaConnectionObject* cnnobj) {
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING");
	print_crossing_event(event);
#endif
	rcw_keyboard_grab_request(cnnobj, FALSE);
	return TRUE;
}

static gboolean rcw_other_fs_focus_in(MultimonWindow*w, GdkEventFocus *event, RemminaConnectionObject* cnnobj)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: focus=%d %d", event->in, event->send_event);
#endif
	if (IS_WAYLAND(w)) {
		rcw_restore_widget_areas(cnnobj->cnnwin);
	}
	rcw_keyboard_grab_request(cnnobj, TRUE);
	return TRUE;
}

/* RCW_MULTIMON object */
static void remmina_fullscreen_window_dispose(GObject *object) {
	MultimonWindow *fswin = RCW_MULTIMON(object);

	if (fswin->priv != NULL && fswin->priv->drawing_area != NULL) {
	    gtk_widget_destroy(fswin->priv->drawing_area);
	fswin->priv->drawing_area = NULL;
	}
	g_signal_handlers_disconnect_by_func(G_OBJECT(fswin), G_CALLBACK(rcw_map_event_fullscreen_monitor), NULL);
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "focus-in-event", G_CALLBACK(rcw_other_fs_focus_in));
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "enter-notify-event", G_CALLBACK(rcw_other_fs_enter));
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "leave-notify-event", G_CALLBACK(rcw_other_fs_leave));

	// Chaîner la méthode parent
	G_OBJECT_CLASS(multimon_window_parent_class)->dispose(object);
}

static void remmina_fullscreen_window_finalize(GObject *object) {
	MultimonWindow *fswin = RCW_MULTIMON(object);

	if (fswin->priv != NULL) {
	    g_free(fswin->priv);
	    fswin->priv = NULL;
	}
	G_OBJECT_CLASS(multimon_window_parent_class)->finalize(object);
}

static void multimon_window_class_init(MultimonWindowClass *klass) {
	TRACE_CALL(__func__);
	G_OBJECT_CLASS(klass)->dispose = remmina_fullscreen_window_dispose;
	G_OBJECT_CLASS(klass)->finalize = remmina_fullscreen_window_finalize;
}

static void multimon_window_init(MultimonWindow *fswin) {
	TRACE_CALL(__func__);
	fswin->priv = g_new0(MultimonWindowPriv, 1);
	fswin->priv->drawing_area = NULL;

	g_signal_connect(G_OBJECT(fswin), "map-event", G_CALLBACK(rcw_map_event_fullscreen_monitor), 0);
}

static void rcw_normalize_monitors(RemminaConnectionWindowPriv*priv, int multimon_main_monitor) {
	// calculation to support translate monitors to 0,0. TODO: same thing for gaps between monitors?
	gboolean hasPrimary = FALSE;
	gint min_x = INT_MAX, min_y = INT_MAX;
	gint mostleft_monitor_y = 0, mosttop_monitor_x = 0;
	gint origin_shift_x = 0;
	gint origin_shift_y = 0;
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));		
		if (monwin->priv->isPrimary)
			hasPrimary = TRUE;
		if (monwin->priv->geometry.x < min_x) {
			min_x = monwin->priv->geometry.x;
			mostleft_monitor_y = monwin->priv->geometry.y;
		}
		if (monwin->priv->geometry.y < min_y) {
			min_y = monwin->priv->geometry.y;
			mosttop_monitor_x = monwin->priv->geometry.x;
		}
	}

	if (abs(mostleft_monitor_y-min_x)<abs(mosttop_monitor_x-min_y)) {
		origin_shift_x = min_x;
		origin_shift_y = mostleft_monitor_y;
	} else {
		origin_shift_x = mosttop_monitor_x;
		origin_shift_y = min_y;
	}
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));		
		if (!hasPrimary && multimon_main_monitor == i) {
			monwin->priv->isPrimary = TRUE;
		}
		monwin->priv->geometry.x -= origin_shift_x;
		monwin->priv->geometry.y -= origin_shift_y;
	}
	priv->multi_mon = priv->multimon_areas->len > 1;
}

static gint rcw_probe_monitors(RemminaConnectionObject *cnnobj, const gchar*monitorids_str) {
	GdkDisplay *display = gdk_display_get_default();
	RemminaConnectionWindowPriv*priv = cnnobj->cnnwin->priv;
	gint multimon_main_monitor = -1;
	gint n_monitors = gdk_display_get_n_monitors(display);
	gint first_monitor = -1;
	gint first_filtered_monitor = INT_MAX;
	gchar **monitorids = NULL;

	if (monitorids_str != NULL) {
		monitorids = g_strsplit(monitorids_str, ",", -1);
	}

	const GdkRectangle primary_monitor_geometry = rcw_first_monitor_geometry();

	priv->multi_mon = 0;
	if (priv->multimon_areas != NULL)
		g_ptr_array_free(priv->multimon_areas, TRUE);
	
	priv->multimon_areas = g_ptr_array_new();
	REMMINA_DEBUG("number of monitors: %d", n_monitors);
	for (gint i = 0; i < n_monitors; ++i) {
		GdkMonitor *monitor = gdk_display_get_monitor(display, i);
	    if (monitor == NULL) {
	        g_warning("monitor %d is not accessible", i);
	        continue;
		}

		const gchar*model = gdk_monitor_get_model(monitor);
		const gchar*manufacturer = gdk_monitor_get_manufacturer(monitor);

		REMMINA_DEBUG("monitor#%d: model=%s manufacturer=%s", i, model, manufacturer);

		if (monitorids != NULL) {
			gboolean found = FALSE;
			for (gint j = 0; monitorids[j] != NULL; j++) {
				gint64 number;
				gboolean is_number;
				is_number = g_ascii_string_to_signed(monitorids[j], 10, G_MININT, G_MAXINT, &number, NULL);
				if ( (is_number &&  number == i) ||
						g_strcmp0(monitorids[j], manufacturer) == 0 ||
						g_strcmp0(monitorids[j], model) == 0) {
						found = TRUE;
						if (j < first_filtered_monitor) {
							first_filtered_monitor = j;
							first_monitor = i;
						}
						break;
				}
			}
			if (!found) {
				REMMINA_DEBUG("monitor %d ignored (from configuration)", i);
				continue;
			}
		}

		if (first_monitor == -1) first_monitor = i;
		MultimonWindow *fullscreenWindow = RCW_MULTIMON(g_object_new(REMMINA_TYPE_MULTIMON_WINDOW, NULL));
		MultimonWindowPriv *wpriv = fullscreenWindow->priv;

		g_signal_connect(GTK_WINDOW(fullscreenWindow), "focus-in-event", G_CALLBACK(rcw_other_fs_focus_in), cnnobj);
		g_signal_connect(GTK_WINDOW(fullscreenWindow), "enter-notify-event", G_CALLBACK(rcw_other_fs_enter), cnnobj);
		g_signal_connect(GTK_WINDOW(fullscreenWindow), "leave-notify-event", G_CALLBACK(rcw_other_fs_leave), cnnobj);

		wpriv->scale = gdk_monitor_get_scale_factor(monitor);
		gdk_monitor_get_geometry(monitor, &wpriv->geometry);
		wpriv->physicalHeight = gdk_monitor_get_height_mm(monitor);
		wpriv->physicalWidth = gdk_monitor_get_width_mm(monitor);
		wpriv->isPrimary = gdk_monitor_is_primary(monitor);

		if (monitorids == NULL && gdk_rectangle_equal(&primary_monitor_geometry, &wpriv->geometry)) {
			wpriv->isPrimary = 1;
		}

		wpriv->monitor = i;
		// main monitor is either the primary, or first configured monitor
		if (wpriv->isPrimary && multimon_main_monitor == -1 && monitorids == NULL)
			multimon_main_monitor = i;
		g_ptr_array_add(priv->multimon_areas, fullscreenWindow);
	}

	if (monitorids != NULL) {
	    g_strfreev(monitorids);
	}

	if (multimon_main_monitor == -1) {
		multimon_main_monitor = first_monitor;
	}

	rcw_normalize_monitors(priv, multimon_main_monitor);

	return multimon_main_monitor;
}

static void rcw_create_widget_areas(RemminaConnectionObject *cnnobj) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv != NULL);

	RemminaConnectionWindow*cnnwin = cnnobj->cnnwin;
	RemminaConnectionWindowPriv*priv = cnnwin->priv;
	gint multimon_main_monitor = -1;
	const gchar*monitorids_str = remmina_file_get_string(cnnobj->remmina_file, "monitorids");


	if (priv->multimon_areas != NULL) {
	    rcw_dispose_widget_areas(priv);
	}
	
	multimon_main_monitor = rcw_probe_monitors(cnnobj, monitorids_str);
	if (multimon_main_monitor ==-1 && monitorids_str!=NULL && strlen(monitorids_str)>0) {
		REMMINA_DEBUG("Multimonitor filtered all monitors, now using all available monitors");
		multimon_main_monitor = rcw_probe_monitors(cnnobj, NULL);
	}

	if (multimon_main_monitor == -1) {
		REMMINA_DEBUG("No monitors where found!");
		return;
	}

	REMMINA_DEBUG("total number of active monitors %d", priv->multimon_areas->len);
	const gchar*title = remmina_file_get_string(cnnobj->remmina_file, "name");
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* fullscreenWindow = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));
		MultimonWindowPriv *wpriv = fullscreenWindow->priv;
		if (wpriv->monitor != multimon_main_monitor) {	// main monitor is handled by the legacy code
	        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(fullscreenWindow), TRUE);
			gtk_widget_set_name(GTK_WIDGET(fullscreenWindow), "remmina-connection-window-fullscreen");
			if (title != NULL) {
				gchar*childTitle = g_malloc(strlen(title)+20);
				sprintf(childTitle, "%s#%d", title, wpriv->monitor);
				gtk_window_set_title(GTK_WINDOW(fullscreenWindow), childTitle);
				g_free(childTitle);
			}
		}
		REMMINA_DEBUG("monitor#%d (%s) primary=%d position=%dx%d size=%dx%d scale=%d",
			wpriv->monitor, wpriv->monitor == multimon_main_monitor?"main":"extra",
			wpriv->isPrimary, wpriv->geometry.x,wpriv->geometry.y,wpriv->geometry.width,wpriv->geometry.height,wpriv->scale);
	}
	priv->multimon_main_monitor = multimon_main_monitor;
}

static void*widget_area_last_restored_cnnwin = NULL;

static void rcw_iconify_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
		return;

	widget_area_last_restored_cnnwin = NULL;
	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor) {
			if (!fswin->priv->iconified) {
				gtk_window_iconify(GTK_WINDOW(fswin));
				if (IS_WAYLAND(fswin)) {
					gtk_widget_hide(GTK_WIDGET(fswin));
				}
				fswin->priv->iconified = TRUE;
			}
		}
	}
}


// Windowing actions in multi monitor when a multimonitor connection gained the focus:
// 	in X11: unraise all other connection fullscreen windows
//  in Wayland: hide all other connection fullscreen windows that are not in common
static gboolean rcw_active_connection_changed(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	if (REMMINA_IS_CONNECTION_WINDOW(widget) && REMMINA_IS_CONNECTION_WINDOW(data)) {
		RemminaConnectionWindow*receiver = RCW(widget);
		RemminaConnectionWindow*sender = RCW(data);
		if (sender != receiver && receiver->priv != NULL && receiver->priv->multimon_areas != NULL) {
			if (IS_WAYLAND(widget)) {
				GHashTable *sender_monitors = g_hash_table_new(g_direct_hash, g_direct_equal);
				if (sender->priv->multimon_areas != NULL) {
					for (guint i = 0; i < sender->priv->multimon_areas->len; i++) {
						MultimonWindow *fswin = g_ptr_array_index(sender->priv->multimon_areas, i);
						g_hash_table_insert(sender_monitors, GINT_TO_POINTER(fswin->priv->monitor), NULL);
					}
				}
				g_hash_table_insert(sender_monitors, GINT_TO_POINTER(sender->priv->multimon_main_monitor), NULL);
				for (guint i = 0; i < receiver->priv->multimon_areas->len; i++) {
					MultimonWindow *fswin = g_ptr_array_index(receiver->priv->multimon_areas, i);
					if (fswin != NULL && g_hash_table_contains(sender_monitors, GINT_TO_POINTER(fswin->priv->monitor))) {
						if (!fswin->priv->iconified) {
							gtk_widget_hide(GTK_WIDGET(fswin));
							fswin->priv->iconified = TRUE;
						}
					}
				}
				g_hash_table_destroy(sender_monitors);
			} else {
				RemminaConnectionWindowPriv*priv = receiver->priv;
				for (guint i = 0; i < priv->multimon_areas->len; i++) {
					MultimonWindow *fswin = g_ptr_array_index(priv->multimon_areas, i);
					if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != priv->multimon_main_monitor) {
						gtk_window_set_keep_above(GTK_WINDOW(fswin), FALSE);
					}
				}
				gtk_window_set_keep_above(GTK_WINDOW(receiver), FALSE);
			}
		}
		return TRUE;
	}
	return FALSE;
}


static void rcw_show_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
	        return;

	REMMINA_DEBUG("rcw_show_widget_areas");
	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			gtk_widget_show(GTK_WIDGET(fswin));
		}
	}
}

static GdkWindow* rcw_widget_active_area(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, FALSE);
	if (cnnwin->priv->multimon_areas == NULL)
	    return NULL;

	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			GdkWindow* w = gtk_widget_get_parent_window(GTK_WIDGET(fswin->priv->drawing_area));
			if (gdk_window_get_state(w) & GDK_WINDOW_STATE_FOCUSED)
				return w;
		}
	}
	return NULL;
}

static void rcw_restore_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
		return;
	if (widget_area_last_restored_cnnwin == cnnwin) {
		return;
	}
	widget_area_last_restored_cnnwin = cnnwin;


	REMMINA_DEBUG("rcw_restore_widget_areas");
	/* Signal all windows that which connection is active */	
	remmina_widget_pool_foreach(rcw_active_connection_changed, cnnwin);

	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			if (fswin->priv->iconified) {
				REMMINA_DEBUG("deiconify");
				fswin->priv->iconified = FALSE;
				gtk_window_deiconify(GTK_WINDOW(fswin));
				if (IS_WAYLAND(fswin)) {
					gtk_widget_show(GTK_WIDGET(fswin));
				}
			}
			// needed to raise again in case other connection is using fullscreen on same monitor(s)
			gtk_window_set_keep_above(GTK_WINDOW(fswin), FALSE);
			gtk_window_set_keep_above(GTK_WINDOW(fswin), TRUE);
		}
	}	
	if (IS_WAYLAND(cnnwin)) {
		gtk_widget_show(GTK_WIDGET(cnnwin));
	}
}

static void rcw_dispose_widget_areas(RemminaConnectionWindowPriv*priv) {
	g_return_if_fail(priv != NULL);
	if (priv->multimon_areas == NULL)
		return;

	for (guint i = 0; i < priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin)) {
			if (GTK_IS_WINDOW(fswin)) {
	            gtk_widget_destroy(GTK_WIDGET(fswin));
	        }
		}
	}
	g_ptr_array_free(priv->multimon_areas, TRUE);
	priv->multimon_areas = NULL;
}

gint rcw_get_monitor_count(RemminaConnectionWindow *cnnwin) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, 0);

	if (cnnwin->priv->multimon_areas == NULL)
		return 0;

	return cnnwin->priv->multimon_areas->len;
}

gboolean rcw_get_monitor_info(RemminaConnectionWindow *cnnwin, gint index, gboolean*is_primary, gint*x, gint*y, gint*width, gint*height, gint*phys_width_mm, gint* phys_height_mm) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, FALSE);

	if (cnnwin->priv->multimon_areas == NULL || index >= cnnwin->priv->multimon_areas->len)
		return FALSE;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(cnnwin->priv->multimon_areas, index));
	g_return_val_if_fail(monwin != NULL && monwin->priv != NULL, FALSE);

	if (is_primary != NULL) *is_primary = (monwin->priv->isPrimary == 1);
	if (x != NULL) *x = cnnwin->priv->multimon_areas->len != 1 ? monwin->priv->geometry.x : 0;
	if (y != NULL) *y = cnnwin->priv->multimon_areas->len != 1 ? monwin->priv->geometry.y : 0;
	if (width != NULL) *width = monwin->priv->geometry.width;
	if (height != NULL) *height = monwin->priv->geometry.height;
	if (phys_width_mm != NULL) *phys_width_mm = monwin->priv->physicalWidth;
	if (phys_height_mm != NULL) *phys_height_mm = monwin->priv->physicalHeight;

	return TRUE;
}

gint multimon_get_monitor_count(RemminaProtocolWidget *proto) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL, 0);
	return rcw_get_monitor_count(proto->cnnobj->cnnwin);
}

gboolean multimon_get_monitor_info(RemminaProtocolWidget *proto, gint index, gboolean*is_primary, gint*x, gint*y, gint*width, gint*height, gint*phys_width_mm, gint* phys_height_mm) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL, FALSE);
	return rcw_get_monitor_info(proto->cnnobj->cnnwin, index, is_primary, x, y, width, height, phys_width_mm, phys_height_mm);
}

int multimon_get_main_monitor_index(RemminaProtocolWidget *proto) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
					&& proto->cnnobj->cnnwin->priv != NULL, -1);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL)
					return -1;

	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;
	for (gint i = 0 ; i<priv->multimon_areas->len ; i++) {
		MultimonWindow* win = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));
		if (win->priv->monitor == priv->multimon_main_monitor)
			return i;
	}
	return -1;
}

void multimon_set_monitor_drawing_area(RemminaProtocolWidget *proto, gint index, GtkWidget*drawing_area) {
	TRACE_CALL(__func__);
	g_return_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
					&& proto->cnnobj->cnnwin->priv != NULL);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL || index >= proto->cnnobj->cnnwin->priv->multimon_areas->len)
		return;
	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, index));
	if (monwin->priv->monitor == priv->multimon_main_monitor)
		return;
	if (monwin->priv->drawing_area != NULL) {
		gtk_widget_destroy(monwin->priv->drawing_area);
		monwin->priv->drawing_area = NULL;
	}
	monwin->priv->drawing_area = drawing_area;
	if (drawing_area != NULL) {
			gtk_container_add(GTK_CONTAINER(monwin), drawing_area);
	}
}

GtkWidget* multimon_get_monitor_drawing_area(RemminaProtocolWidget *proto, gint index) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
				     && proto->cnnobj->cnnwin->priv != NULL, NULL);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL || index >= proto->cnnobj->cnnwin->priv->multimon_areas->len)
		return NULL;
	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, index));
	if (monwin->priv->monitor == priv->multimon_main_monitor)
		return NULL;
	if (monwin->priv->drawing_area == NULL) {
		monwin->priv->drawing_area = gtk_drawing_area_new();		
		gtk_container_add(GTK_CONTAINER(monwin), monwin->priv->drawing_area);
		gtk_widget_realize(monwin->priv->drawing_area);
		if (proto->cnnobj->connected)
			gtk_widget_show(GTK_WIDGET(monwin));
	}
	return monwin->priv->drawing_area;
}
