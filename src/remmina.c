/*
 * Remmina - The GTK Remote Desktop Client
 * Copyright (C) 2014-2023 Antenore Gatta, Giovanni Panozzo
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
#include <gdk/gdk.h>

#define G_LOG_USE_STRUCTURED
#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN    ((gchar*)"unidesk")
#endif  /* G_LOG_DOMAIN */
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#elif defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <stdlib.h>

#include "config.h"
#include "remmina_sodium.h"
#include "remmina.h"
#include "remmina_exec.h"
#include "remmina_file_manager.h"
#include "remmina_icon.h"
#include "remmina_log.h"
#include "remmina_main.h"
#include "remmina_masterthread_exec.h"
#include "remmina_plugin_manager.h"
#include "remmina_plugin_native.h"
#include "remmina_pref.h"
#include "remmina_theme.h"
#include "remmina_public.h"
#include "remmina_sftp_plugin.h"
#include "remmina_ssh_plugin.h"
#include "remmina_telnet_plugin.h"
#include "remmina_runtime.h"
#include "remmina_scale.h"
#include "remmina_unlock.h"
#include "remmina_vault.h"
#include "remmina_widget_pool.h"
#include "remmina/remmina_trace_calls.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <pthread.h>
#ifdef HAVE_MALLOPT
#include <malloc.h>
#endif
#ifdef HAVE_LIBGCRYPT
#include <gcrypt.h>
# if GCRYPT_VERSION_NUMBER < 0x010600
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif  /* !GCRYPT_VERSION_NUMBER */
#endif  /* HAVE_LIBGCRYPT */

#ifdef HAVE_LIBGCRYPT
# if GCRYPT_VERSION_NUMBER < 0x010600
static int gcrypt_thread_initialized = 0;
#endif  /* !GCRYPT_VERSION_NUMBER */
#endif  /* HAVE_LIBGCRYPT */

gboolean imode;
gboolean disabletoolbar;
gboolean fullscreen;
gboolean extrahardening;
gboolean disabletrayicon;

static GOptionEntry remmina_options[] =
{
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "about",	      'a',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Show \'About\'"),								     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "connect",	      'c',  0,			  G_OPTION_ARG_FILENAME_ARRAY, NULL, N_("Connect either to a desktop described in a file (.remmina or a filetype supported by a plugin) or a supported URI (RDP, SSH or TELNET)"),	     N_("FILE")	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ G_OPTION_REMAINING, '\0', 0,			  G_OPTION_ARG_FILENAME_ARRAY, NULL, N_("Connect to a desktop described in a file (.remmina or a filetype supported by a plugin)"),	     N_("FILE")	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "edit",	      'e',  0,			  G_OPTION_ARG_FILENAME_ARRAY, NULL, N_("Edit desktop connection described in file (.remmina or a filetype supported by plugin)"), N_("FILE")	},
	{ "help",	      '?',  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,	       NULL, NULL,										     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "new",	      'n',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Create new connection profile"),						     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "pref",	      'p',  0,			  G_OPTION_ARG_STRING,	       NULL, N_("Show preferences"),								     N_("TABINDEX")	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "quit",	      'q',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Quit"),									     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "server",	      's',  0,			  G_OPTION_ARG_STRING,	       NULL, N_("Use default server name (for --new)"),						     N_("SERVER")	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "protocol",	      't',  0,			  G_OPTION_ARG_STRING,	       NULL, N_("Use default protocol (for --new)"),						     N_("PROTOCOL") },
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "icon",	      'i',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Start in tray"),								     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "version",	      'v',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Show the application version"),						     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "full-version",     'V',  0,			  G_OPTION_ARG_NONE,	       NULL, N_("Show version of the application and its plugins"),				     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "update-profile",   0,    0,			  G_OPTION_ARG_FILENAME,       NULL, N_("Modify connection profile (requires --set-option)"),				     NULL	},
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	{ "set-option",	      0,    0,			  G_OPTION_ARG_STRING_ARRAY,   NULL, N_("Set one or more profile settings, to be used with --update-profile"),		     NULL	},
	{ "encrypt-password", 0,    0,			  G_OPTION_ARG_NONE,	       NULL, N_("Encrypt a password"),												  NULL		 },
	{ "disable-toolbar", 0,    0,			  G_OPTION_ARG_NONE,	       NULL, N_("Disable toolbar"),												  NULL		 },
	{ "enable-fullscreen", 0,    0,			  G_OPTION_ARG_NONE,	       NULL, N_("Enable fullscreen"),												  NULL		 },
	{ "enable-extra-hardening", 0,    0,		  G_OPTION_ARG_NONE,	       NULL, N_("Enable extra hardening (disable closing confirmation, disable unsafe shortcut keys, hide tabs, hide search bar)"),	  NULL		 },
	{ "no-tray-icon", 0,    0,			  G_OPTION_ARG_NONE,	       NULL, N_("Disable tray icon"),												  NULL		 },
	{ NULL }
};

#ifdef WITH_LIBGCRYPT
static int
_gpg_error_to_errno(gcry_error_t e)
{
	/* be lazy right now */
	if (e == GPG_ERR_NO_ERROR)
		return 0;
	else
		return EINVAL;
}
#endif /* !WITH_LIBGCRYPT */

static gint remmina_on_command_line(GApplication *app, GApplicationCommandLine *cmdline)
{
	TRACE_CALL(__func__);

	gint status = 0;
	gboolean executed = FALSE;
	GVariantDict *opts;
	gchar *str;
	const gchar **files;
	const gchar **remaining_args;
	gchar *protocol;
	gchar *server;

#if SODIUM_VERSION_INT >= 90200
	remmina_sodium_init();
#endif
	opts = g_application_command_line_get_options_dict(cmdline);

	if (g_variant_dict_lookup_value(opts, "disable-toolbar", NULL)) {
		disabletoolbar = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "enable-fullscreen", NULL)) {
		fullscreen = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "enable-extra-hardening", NULL)) {
		extrahardening = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "no-tray-icon", NULL)) {
		disabletrayicon = TRUE;
	}

	remmina_pref_init();

	if (g_variant_dict_lookup_value(opts, "quit", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_EXIT, NULL);
		executed = TRUE;
		status = 1;
	}

	if (g_variant_dict_lookup_value(opts, "about", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_ABOUT, NULL);
		executed = TRUE;
	}

	/** @warning To be used like -c FILE -c FILE -c FILE …
	 *
	 */
	if (g_variant_dict_lookup(opts, "connect", "^aay", &files)) {
		if (files)
			for (gint i = 0; files[i]; i++) {
				g_debug ("Connecting to: %s", files[i]);
				remmina_exec_command(REMMINA_COMMAND_CONNECT, files[i]);
			}
		executed = TRUE;
	}

	if (g_variant_dict_lookup(opts, G_OPTION_REMAINING, "^a&ay", &remaining_args)) {
		remmina_exec_command(REMMINA_COMMAND_CONNECT, remaining_args[0]);
		g_free(remaining_args);
		executed = TRUE;
	}

	if (g_variant_dict_lookup(opts, "edit", "^aay", &files)) {
		if (files)
			for (gint i = 0; files[i]; i++) {
				g_debug ("Editing file: %s", files[i]);
				remmina_exec_command(REMMINA_COMMAND_EDIT, files[i]);
			}
		executed = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "new", NULL)) {
		if (!g_variant_dict_lookup(opts, "protocol", "&s", &protocol))
			protocol = NULL;

		if (g_variant_dict_lookup(opts, "server", "&s", &server))
			str = g_strdup_printf("%s,%s", protocol, server);
		else
			str = g_strdup(protocol);

		remmina_exec_command(REMMINA_COMMAND_NEW, str);
		g_free(str);
		executed = TRUE;
	}

	if (g_variant_dict_lookup(opts, "pref", "&s", &str)) {
		remmina_exec_command(REMMINA_COMMAND_PREF, str);
		executed = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "icon", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_NONE, NULL);
		executed = TRUE;
	}

	if (g_variant_dict_lookup_value(opts, "encrypt-password", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_ENCRYPT_PASSWORD, NULL);
		executed = TRUE;
		status = 1;
	}

	if (!executed)
		remmina_exec_command(REMMINA_COMMAND_MAIN, NULL);

	return status;
}

static gboolean remmina_sigint_cb(gpointer data)
{
	remmina_application_condexit(REMMINA_CONDEXIT_ONQUIT);
	return G_SOURCE_REMOVE;
}

static void remmina_on_startup(GApplication *app)
{
	TRACE_CALL(__func__);

	RemminaSecretPlugin *secret_plugin;

	/* GTK's own strings (dialog buttons, file chooser…): the AppImage carries their Russian
	 * translation, the host may not have it. gtk_init() has bound "gtk30" to the host dir. */
	{
		const gchar *localedir = remmina_runtime_dir(REMMINA_RUNTIME_DIR_LOCALE);
		gchar *mo = g_build_filename(localedir, "ru", "LC_MESSAGES", "gtk30.mo", NULL);
		if (g_file_test(mo, G_FILE_TEST_EXISTS)) {
			bindtextdomain("gtk30", localedir);
			bind_textdomain_codeset("gtk30", "UTF-8");
		}
		g_free(mo);
	}

	remmina_widget_pool_init();
	/* Unidesk always uses the dark theme */
	remmina_theme_init();
	remmina_sftp_plugin_register();
	remmina_ssh_plugin_register();
	remmina_telnet_plugin_register();
	remmina_icon_init();

	gtk_window_set_default_icon_name(REMMINA_CLASS_ID);

	/* Setting the X11 program class (WM_CLASS) is necessary to group
	* windows with .desktop file which has the same StartupWMClass */
	gdk_set_program_class(REMMINA_CLASS_ID);

	g_unix_signal_add(SIGINT, remmina_sigint_cb, NULL);
	g_unix_signal_add(SIGTERM, remmina_sigint_cb, NULL);

	{
		/* Our icons, wherever the application is installed or unpacked (AppImage) */
		gchar *icons_dir = g_build_filename(remmina_runtime_dir(REMMINA_RUNTIME_DIR_DATA), "icons", NULL);
		gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), icons_dir);
		g_free(icons_dir);
	}
	g_application_hold(app);

	gchar *log_filename = g_build_filename(g_get_tmp_dir(), LOG_FILE_NAME, NULL);
	FILE *log_file = fopen(log_filename, "w"); // Flush log file
	g_free(log_filename);

	if (log_file != NULL) {
		fclose(log_file);
	}

	/* Check for secret plugin and service initialization and show console warnings if
	 * something is missing */	
	secret_plugin = remmina_plugin_manager_get_secret_plugin();
	if (!secret_plugin)
		g_print("Warning: UniDeskBrowser is running without a secret plugin. Passwords will be saved in a less secure way.\n");
	else
		if (!secret_plugin->is_service_available(secret_plugin))
			g_print("Warning: the password vault is locked. Unlock it to use the saved passwords.\n");

	/* Import the legacy per-file Remmina profiles once, now that the protocol
	 * plugins and the vault are known */
	remmina_file_manager_import_legacy();
	remmina_file_manager_migrate_rdp_quality();

	/* The single application window exists from the start; it is shown by
	 * REMMINA_COMMAND_MAIN or as soon as a connection or a tool tab opens */
	remmina_main_new();

	/* A vault protected by the primary password is opened once at start-up: the
	 * usual unlock dialog also opens the vault; fall back to the vault prompt
	 * when the two passwords differ (preferences edited by hand) */
	if (remmina_vault_has_master_password() && !remmina_vault_is_unlocked()) {
		GtkWindow *parent = remmina_main_get_window();
		if (!remmina_unlock_new(parent) || !remmina_vault_is_unlocked())
			remmina_vault_prompt_unlock(parent);
	}

	remmina_exec_command(REMMINA_COMMAND_AUTOSTART, NULL);
}

static gint remmina_on_local_cmdline(GApplication *app, GVariantDict *opts, gpointer user_data)
{
	TRACE_CALL(__func__);

	int status = -1;
	gchar *str;
	gchar **settings;

	/* Here you handle any command line options that you want to be executed
	 * in the local instance (the non-unique instance) */

	if (g_variant_dict_lookup_value(opts, "version", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_VERSION, NULL);
		status = 0;
	}

	if (g_variant_dict_lookup_value(opts, "full-version", NULL)) {
		remmina_exec_command(REMMINA_COMMAND_FULL_VERSION, NULL);
		status = 0;
	}

	/* Here, in the process the user started: a running instance would read the password on its
	 * own stdin and then quit with all its windows */
	if (g_variant_dict_lookup_value(opts, "encrypt-password", NULL)) {
		remmina_exec_encrypt_password();
		status = 0;
	}

	if (g_variant_dict_lookup(opts, "update-profile", "^&ay", &str)) { /* ^&ay no need to free */
		if (g_variant_dict_lookup(opts, "set-option", "^a&s", &settings)) {
			if (settings != NULL) {
				status = remmina_exec_set_setting(str, settings);
				g_free(settings);
			} else {
				status = 1;
			}
		} else {
			status = 1;
			g_print("Error: --update-profile requires --set-option\n");
		}
	}

	/* Returning a non negative value here makes the application exit */
	return status;
}

int main(int argc, char *argv[])
{
	TRACE_CALL(__func__);
	GtkApplication *app;
	const gchar *app_id;
	int status;

	// Remmina does not need the default arena number (CPU cores x 8), 2 seem optimal for us
#ifdef HAVE_MALLOPT
	if (!g_getenv("MALLOC_ARENA_MAX"))
		mallopt(M_ARENA_MAX, 2);
#endif

	g_unsetenv("GDK_CORE_DEVICE_EVENTS");
	/* Through XWayland: a GDK_SCALE too large for the screens is lowered (before GTK reads it) */
	remmina_scale_adapt();

	// Checking for environment variable "G_MESSAGES_DEBUG"
	// Give the less familiar with GLib a tip on where to get
	// more debugging info.
	if(!getenv("G_MESSAGES_DEBUG")) {
		/* TRANSLATORS:
		 * This link should point to a resource explaining how to get Remmina
		 * to log more verbose statements.
		 */
		g_message(_("UniDeskBrowser does not log all output statements. "
			    "Turn on more verbose output by using "
			    "\"G_MESSAGES_DEBUG=unidesk\" as an environment variable."
		));
	}

	/* Enable wayland backend only after GTK 3.22.27 or the clipboard
	 * will not work. See GTK bug 790031 */
	if (remmina_gtk_check_version(3, 22, 27))
		gdk_set_allowed_backends("wayland,x11,broadway,quartz,mir");
	else
		gdk_set_allowed_backends("x11,broadway,quartz,mir");

	remmina_masterthread_exec_save_main_thread_id();

	bindtextdomain(GETTEXT_PACKAGE, remmina_runtime_dir(REMMINA_RUNTIME_DIR_LOCALE));
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

#ifdef HAVE_LIBGCRYPT
# if GCRYPT_VERSION_NUMBER < 0x010600
	gcry_error_t e;
	if (!gcrypt_thread_initialized) {
		if ((e = gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread)) != GPG_ERR_NO_ERROR)
			return -1;
		gcrypt_thread_initialized++;
	}
#endif  /* !GCRYPT_VERSION_NUMBER */
	gcry_check_version(NULL);
	gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
	gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
#endif  /* !HAVE_LIBGCRYPT */

	/* Initialize some Remmina parts needed also on a local instance for correct handle-local-options */
	remmina_pref_init();
	remmina_pref_apply_language();
	remmina_file_manager_init();
	remmina_plugin_manager_init();

	app_id =g_application_id_is_valid(REMMINA_APP_ID) ? REMMINA_APP_ID : NULL;
	app = gtk_application_new(app_id, G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_CAN_OVERRIDE_APP_ID);
#if !GTK_CHECK_VERSION(4, 0, 0) /* This is not needed anymore starting from GTK 4 */
	g_set_prgname(REMMINA_CLASS_ID);
#endif
	/* Before the command line is handled: --version prints this name */
	g_set_application_name("UniDeskBrowser");
	g_application_add_main_option_entries(G_APPLICATION(app), remmina_options);
#if GLIB_CHECK_VERSION(2,56,0)
	gchar *summary = g_strdup_printf ("%s %s", g_get_application_name(), VERSION);
	g_application_set_option_context_summary (G_APPLICATION(app), summary);
	g_free(summary);
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	g_application_set_option_context_parameter_string (G_APPLICATION(app), _("- or protocol://username:encryptedpassword@host:port"));
	// TRANSLATORS: Shown in terminal. Do not use characters that may be not supported on a terminal
	g_application_set_option_context_description (G_APPLICATION(app),
			// xgettext:no-c-format
			_("Examples:\n"
				"To connect using an existing connection profile, use:\n"
				"\n"
				"\tunidesk -cFILE.remmina\n"
				"\n"
				"To quick connect using a URI:\n"
				"\n"
				"\tunidesk -crdp://username@server\n"
				"\tunidesk -crdp://domain\\\\username@server\n"
				"\tunidesk -cssh://user@server\n"
				"\tunidesk -ctelnet://user@server\n"
				"\n"
				"To quick connect and name the connection\n"
				"\n"
				"\tunidesk -cssh://user@server\%connection_name\n"
				"\n"
				"To quick connect using a URI along with an encrypted password:\n"
				"\n"
				"\tunidesk -crdp://username:encrypted-password@server\n"
				"\tunidesk -cssh://username:encrypted-password@server\n"
				"\n"
				"To encrypt a password for use with a URI:\n"
				"\n"
				"\tunidesk --encrypt-password\n"
				"\n"
				"To update username and password and set a different resolution mode of a UniDeskBrowser connection profile, use:\n"
				"\n"
				"\techo \"username\\napassword\" | unidesk --update-profile /PATH/TO/FOO.remmina --set-option username --set-option resolution_mode=2 --set-option password\n"));
#endif

	g_signal_connect(app, "startup", G_CALLBACK(remmina_on_startup), NULL);
	g_signal_connect(app, "command-line", G_CALLBACK(remmina_on_command_line), NULL);
	g_signal_connect(app, "handle-local-options", G_CALLBACK(remmina_on_local_cmdline), NULL);


	g_application_set_inactivity_timeout(G_APPLICATION(app), 10000);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return status;
}
