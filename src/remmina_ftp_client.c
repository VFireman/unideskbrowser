/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009-2010 Vic Lee
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>

#include "remmina_ftp_client.h"
#include "remmina_ftp_dialogs.h"
#include "remmina_ftp_panel.h"
#include "remmina_pref.h"
#include "remmina_scale.h"
#include "remmina/remmina_trace_calls.h"

G_DEFINE_TYPE(RemminaFTPClient, remmina_ftp_client, GTK_TYPE_BOX)

struct _RemminaFTPClientPriv {
	RemminaFTPPanel *	left;	/* this computer */
	RemminaFTPPanel *	right;	/* the server */
	RemminaFTPPanel *	active;
	RemminaFTPFs *		local_fs;
	GtkWidget *		paned;
	gboolean		paned_placed;

	GtkWidget *		bg_box;	/* the line of a job in the background */
	GtkWidget *		bg_label;
	GtkWidget *		bg_bar;
	GList *			jobs;	/* RemminaFTPJob, queued or running, newest last */

	gboolean		overwrite_all;
	gboolean		resume_all;
	gboolean		disposed;

	gchar *			temp_dir;	/* copies of server files for F3 and F4, made on first use */
	guint			temp_count;
	GList *			edited;		/* EditedFile: copies open in an editor (F4) */

	gchar *			start_remote;	/* a restored tab: the server folder to open first */
};

/* What a job is for, besides copying */
typedef enum {
	OPEN_NONE,
	OPEN_VIEW,	/* F3 on a server file: open the downloaded copy */
	OPEN_EDIT,	/* F4 on a server file: open the copy in the editor, upload it when saved */
	OPEN_UPLOAD,	/* the saved copy goes back to the server */
} OpenMode;

/* What the user interface keeps of a job (job->ui_data) */
typedef struct {
	RemminaFTPClient *	client;		/* NULL once the client is gone */
	RemminaFTPProgress *	progress;
	gboolean		background;
	gint			update_pending;
	RemminaFTPSide		reload_side;	/* the panel whose marks the job used */
	OpenMode		open;
	gchar *			open_local;	/* OPEN_VIEW, OPEN_EDIT: the copy */
	gchar *			open_remote;	/* OPEN_EDIT: the server file */
} JobUI;

/* A server file's copy open in an editor: its saves go back to the server */
typedef struct {
	RemminaFTPClient *	client;
	gchar *			local;
	gchar *			remote;
	GFileMonitor *		monitor;
	guint			check_id;
	gint64			mtime;	/* of the copy as last downloaded or uploaded */
	goffset			size;
	gboolean		asking;
} EditedFile;

static void client_start_job_full(RemminaFTPClient *client, RemminaFTPJob *job, RemminaFTPSide marks_side, OpenMode open,
				  const gchar *open_local, const gchar *open_remote);
static void client_watch_edited(RemminaFTPClient *client, const gchar *local, const gchar *remote);

static void client_update_background_line(RemminaFTPClient *client);

/* ---- Small helpers ---- */

static GtkWindow *client_toplevel(RemminaFTPClient *client)
{
	GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(client));

	return GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL;
}

static RemminaFTPPanel *client_other(RemminaFTPClient *client, RemminaFTPPanel *panel)
{
	return panel == client->priv->left ? client->priv->right : client->priv->left;
}

static RemminaFTPSide client_side(RemminaFTPClient *client, RemminaFTPPanel *panel)
{
	return panel == client->priv->left ? REMMINA_FTP_SIDE_LOCAL : REMMINA_FTP_SIDE_REMOTE;
}

static RemminaFTPPanel *client_panel_of_side(RemminaFTPClient *client, RemminaFTPSide side)
{
	return side == REMMINA_FTP_SIDE_LOCAL ? client->priv->left : client->priv->right;
}

/* The key as on a Latin layout: Ctrl+H and Alt+A work with the Russian one too */
static guint client_latin_keyval(GdkEventKey *event)
{
	GdkKeymap *keymap = gdk_keymap_get_for_display(gdk_window_get_display(event->window));
	guint keyval = event->keyval;

	gdk_keymap_translate_keyboard_state(keymap, event->hardware_keycode, event->state & ~GDK_SHIFT_MASK, 0, &keyval,
					    NULL, NULL, NULL);
	return gdk_keyval_to_lower(keyval);
}

/* Quietly: a folder a restored tab wants may be gone */
static gboolean client_folder_exists(RemminaFTPFs *fs, const gchar *dir)
{
	RemminaFTPEntry entry = { 0 };
	gboolean ok;

	if (!fs || !dir || dir[0] != '/')
		return FALSE;
	ok = remmina_ftp_fs_stat(fs, dir, &entry, NULL) && entry.type == REMMINA_FTP_ENTRY_DIR;
	g_free(entry.name);
	g_free(entry.owner);
	g_free(entry.group);
	return ok;
}

static gchar *client_resolve_dir(RemminaFTPPanel *panel, const gchar *dir)
{
	if (dir[0] == '/')
		return g_strdup(dir);
	return remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), dir);
}

/* ---- Other programs (F3, F4) ---- */

/* Inside the AppImage the bundled GTK's variables (GTK_PATH, GDK_PIXBUF_MODULE_FILE…) must not reach the
 * programs we start: a system editor would load our modules. Lists of folders keep their system part. */
static GAppLaunchContext *client_launch_context(RemminaFTPClient *client)
{
	static const gchar *const ours[] = {
		"GDK_BACKEND", "GTK_THEME", "APPIMAGE_GTK_THEME", "APPDIR", "APPIMAGE", "ARGV0", "OWD", NULL
	};
	GAppLaunchContext *ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET(client))));
	const gchar *appdir = g_getenv("UNIDESK_APPDIR");
	gchar **env;
	guint i, j;

	remmina_scale_launch_context(ctx);
	if (!appdir || !appdir[0])
		return ctx;
	for (i = 0; ours[i]; i++)
		g_app_launch_context_unsetenv(ctx, ours[i]);
	env = g_get_environ();
	for (i = 0; env[i]; i++) {
		gchar *eq = strchr(env[i], '=');
		gchar *name;

		if (!eq || !strstr(eq + 1, appdir))
			continue;
		name = g_strndup(env[i], eq - env[i]);
		if (strchr(eq + 1, ':')) {
			gchar **parts = g_strsplit(eq + 1, ":", -1);
			GString *kept = g_string_new(NULL);
			for (j = 0; parts[j]; j++) {
				if (!parts[j][0] || g_str_has_prefix(parts[j], appdir))
					continue;
				if (kept->len)
					g_string_append_c(kept, ':');
				g_string_append(kept, parts[j]);
			}
			if (kept->len)
				g_app_launch_context_setenv(ctx, name, kept->str);
			else
				g_app_launch_context_unsetenv(ctx, name);
			g_string_free(kept, TRUE);
			g_strfreev(parts);
		} else {
			g_app_launch_context_unsetenv(ctx, name);
		}
		g_free(name);
	}
	g_strfreev(env);
	return ctx;
}

/* F3: the program the system opens this kind of file with */
static gboolean client_open_default(RemminaFTPClient *client, const gchar *path, GError **error)
{
	gchar *uri = g_filename_to_uri(path, NULL, error);
	GAppLaunchContext *ctx;
	gboolean ok;

	if (!uri)
		return FALSE;
	ctx = client_launch_context(client);
	ok = g_app_info_launch_default_for_uri(uri, ctx, error);
	g_object_unref(ctx);
	g_free(uri);
	return ok;
}

/* F4: the editor of the Preferences, else the default text editor, else the default program */
static gboolean client_open_editor(RemminaFTPClient *client, const gchar *path, GError **error)
{
	const gchar *command = remmina_pref.sftp_editor;
	GAppLaunchContext *ctx;
	GAppInfo *app;
	GList *files;
	gboolean ok;

	if (command && command[0]) {
		/* The file goes at the end of the command line */
		app = g_app_info_create_from_commandline(command, NULL, G_APP_INFO_CREATE_NONE, error);
		if (!app)
			return FALSE;
	} else {
		app = g_app_info_get_default_for_type("text/plain", FALSE);
		if (!app)
			return client_open_default(client, path, error);
	}
	files = g_list_prepend(NULL, g_file_new_for_path(path));
	ctx = client_launch_context(client);
	ok = g_app_info_launch(app, files, ctx, error);
	g_object_unref(ctx);
	g_list_free_full(files, g_object_unref);
	g_object_unref(app);
	return ok;
}

/* A new folder for the copy of one server file: <tmp>/unidesk-sftp-XXXXXX/<n> */
static gchar *client_temp_copy_dir(RemminaFTPClient *client)
{
	gchar *dir;

	if (!client->priv->temp_dir && !(client->priv->temp_dir = g_dir_make_tmp("unidesk-sftp-XXXXXX", NULL)))
		return NULL;
	dir = g_strdup_printf("%s/%u", client->priv->temp_dir, ++client->priv->temp_count);
	if (g_mkdir_with_parents(dir, 0700) != 0) {
		g_free(dir);
		return NULL;
	}
	return dir;
}

static void client_remove_tree(const gchar *path)
{
	GDir *dir = g_dir_open(path, 0, NULL);

	if (dir) {
		const gchar *name;
		while ((name = g_dir_read_name(dir))) {
			gchar *child = g_build_filename(path, name, NULL);
			if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK))
				client_remove_tree(child);
			else
				g_unlink(child);
			g_free(child);
		}
		g_dir_close(dir);
	}
	g_rmdir(path);
}

/* ---- Calls from the transfer thread that need the user ---- */

typedef struct {
	gint			ref;
	GMutex			lock;
	GCond			cond;
	gboolean		done;
	gboolean		abandoned;
	RemminaFTPJob *		job;
	gboolean		conflict;	/* else: an error */
	gchar *			src_path;
	gchar *			dst_path;
	RemminaFTPEntry *	src;
	RemminaFTPEntry *	dst;
	gchar *			message;
	gint			answer;
	gchar *			new_name;
} MainCall;

static void main_call_unref(MainCall *mc)
{
	if (!g_atomic_int_dec_and_test(&mc->ref))
		return;
	g_mutex_clear(&mc->lock);
	g_cond_clear(&mc->cond);
	remmina_ftp_job_unref(mc->job);
	g_free(mc->src_path);
	g_free(mc->dst_path);
	remmina_ftp_entry_free(mc->src);
	remmina_ftp_entry_free(mc->dst);
	g_free(mc->message);
	g_free(mc->new_name);
	g_free(mc);
}

static gboolean main_call_run(gpointer data)
{
	MainCall *mc = data;
	JobUI *ui = mc->job->ui_data;
	gint answer = mc->conflict ? REMMINA_FTP_CONFLICT_CANCEL : REMMINA_FTP_ERROR_CANCEL;
	gchar *new_name = NULL;
	gboolean abandoned;

	g_mutex_lock(&mc->lock);
	abandoned = mc->abandoned;
	g_mutex_unlock(&mc->lock);

	if (!abandoned && ui && ui->client) {
		/* Over the progress window when it shows, else over the tab */
		GtkWindow *parent = ui->progress && remmina_ftp_progress_is_visible(ui->progress) ?
				    remmina_ftp_progress_get_window(ui->progress) : client_toplevel(ui->client);
		if (mc->conflict)
			answer = remmina_ftp_dialog_conflict(parent, mc->src_path, mc->src, mc->dst_path, mc->dst, &new_name);
		else
			answer = remmina_ftp_dialog_error(parent, mc->message);
	}

	g_mutex_lock(&mc->lock);
	mc->answer = answer;
	mc->new_name = new_name;
	mc->done = TRUE;
	g_cond_broadcast(&mc->cond);
	g_mutex_unlock(&mc->lock);
	main_call_unref(mc);
	return G_SOURCE_REMOVE;
}

/* Blocks the transfer thread until the user answers, or the job is cancelled */
static gint main_call_wait(MainCall *mc, gchar **new_name)
{
	gint answer;

	g_atomic_int_inc(&mc->ref);
	g_idle_add(main_call_run, mc);
	g_mutex_lock(&mc->lock);
	while (!mc->done) {
		gint64 until = g_get_monotonic_time() + 200 * G_TIME_SPAN_MILLISECOND;
		gboolean cancelled;
		g_cond_wait_until(&mc->cond, &mc->lock, until);
		g_mutex_lock(&mc->job->lock);
		cancelled = mc->job->cancelled;
		g_mutex_unlock(&mc->job->lock);
		if (cancelled && !mc->done) {
			mc->abandoned = TRUE;
			break;
		}
	}
	answer = mc->done ? mc->answer : (mc->conflict ? REMMINA_FTP_CONFLICT_CANCEL : REMMINA_FTP_ERROR_CANCEL);
	if (new_name && mc->done) {
		*new_name = mc->new_name;
		mc->new_name = NULL;
	}
	g_mutex_unlock(&mc->lock);
	main_call_unref(mc);
	return answer;
}

static MainCall *main_call_new(RemminaFTPJob *job, gboolean conflict)
{
	MainCall *mc = g_new0(MainCall, 1);

	mc->ref = 1;
	g_mutex_init(&mc->lock);
	g_cond_init(&mc->cond);
	mc->job = remmina_ftp_job_ref(job);
	mc->conflict = conflict;
	return mc;
}

static RemminaFTPConflict client_job_ask_conflict(RemminaFTPJob *job, const gchar *src_path, const RemminaFTPEntry *src,
						  const gchar *dst_path, const RemminaFTPEntry *dst, gchar **new_name)
{
	MainCall *mc = main_call_new(job, TRUE);

	mc->src_path = g_strdup(src_path);
	mc->dst_path = g_strdup(dst_path);
	mc->src = remmina_ftp_entry_copy(src);
	mc->dst = remmina_ftp_entry_copy(dst);
	return (RemminaFTPConflict)main_call_wait(mc, new_name);
}

static RemminaFTPErrorAnswer client_job_ask_error(RemminaFTPJob *job, const gchar *message)
{
	MainCall *mc = main_call_new(job, FALSE);

	mc->message = g_strdup(message);
	return (RemminaFTPErrorAnswer)main_call_wait(mc, NULL);
}

/* ---- Progress and the end of a job ---- */

static gboolean client_job_update_idle(gpointer data)
{
	RemminaFTPJob *job = data;
	JobUI *ui = job->ui_data;

	g_atomic_int_set(&ui->update_pending, 0);
	if (ui->client) {
		if (ui->progress)
			remmina_ftp_progress_update(ui->progress);
		if (ui->background)
			client_update_background_line(ui->client);
	}
	remmina_ftp_job_unref(job);
	return G_SOURCE_REMOVE;
}

static void client_job_progress(RemminaFTPJob *job)
{
	JobUI *ui = job->ui_data;

	if (g_atomic_int_compare_and_exchange(&ui->update_pending, 0, 1)) {
		remmina_ftp_job_ref(job);
		g_idle_add(client_job_update_idle, job);
	}
}

static gboolean client_job_finished_idle(gpointer data)
{
	RemminaFTPJob *job = data;
	JobUI *ui = job->ui_data;
	RemminaFTPClient *client = ui->client;

	if (ui->progress) {
		remmina_ftp_progress_free(ui->progress);
		ui->progress = NULL;
	}
	if (client) {
		GList *link = g_list_find(client->priv->jobs, job);
		gboolean cancelled;

		if (link) {
			client->priv->jobs = g_list_delete_link(client->priv->jobs, link);
			remmina_ftp_job_unref(job);
		}
		g_mutex_lock(&job->lock);
		cancelled = job->cancelled;
		g_mutex_unlock(&job->lock);
		/* Done: the marks of the source panel have served, as in Total Commander */
		if (!cancelled && ui->open == OPEN_NONE && (job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE))
			remmina_ftp_panel_unmark_all(client_panel_of_side(client, ui->reload_side));
		remmina_ftp_panel_reload(client->priv->left);
		remmina_ftp_panel_reload(client->priv->right);
		client_update_background_line(client);
		if (client->priv->active)
			remmina_ftp_panel_grab_focus(client->priv->active);
		/* F3 / F4: the copy of the server file is here, open it */
		if (!cancelled && (ui->open == OPEN_VIEW || ui->open == OPEN_EDIT) &&
		    g_file_test(ui->open_local, G_FILE_TEST_IS_REGULAR)) {
			GError *error = NULL;
			gboolean opened = ui->open == OPEN_EDIT ? client_open_editor(client, ui->open_local, &error) :
					  client_open_default(client, ui->open_local, &error);
			if (!opened) {
				remmina_ftp_dialog_message(client_toplevel(client), error->message);
				g_clear_error(&error);
			} else if (ui->open == OPEN_EDIT) {
				client_watch_edited(client, ui->open_local, ui->open_remote);
			}
		}
	}
	remmina_ftp_job_unref(job);
	return G_SOURCE_REMOVE;
}

static void client_job_finished(RemminaFTPJob *job)
{
	g_idle_add(client_job_finished_idle, remmina_ftp_job_ref(job));
}

static void job_ui_free(gpointer data)
{
	JobUI *ui = data;

	if (ui->progress)
		remmina_ftp_progress_free(ui->progress);
	g_free(ui->open_local);
	g_free(ui->open_remote);
	g_free(ui);
}

static void client_on_job_background(RemminaFTPProgress *progress, gpointer data)
{
	RemminaFTPJob *job = data;
	JobUI *ui = job->ui_data;

	ui->background = TRUE;
	if (ui->client) {
		client_update_background_line(ui->client);
		if (ui->client->priv->active)
			remmina_ftp_panel_grab_focus(ui->client->priv->active);
	}
}

/* The background line shows the first job that runs out of sight */
static RemminaFTPJob *client_background_job(RemminaFTPClient *client)
{
	GList *l;

	for (l = client->priv->jobs; l; l = l->next) {
		RemminaFTPJob *job = l->data;
		JobUI *ui = job->ui_data;
		if (ui->background)
			return job;
	}
	return NULL;
}

static void client_update_background_line(RemminaFTPClient *client)
{
	RemminaFTPJob *job = client_background_job(client);
	gchar *speed, *left, *text;
	gint percent;

	if (!job) {
		gtk_widget_hide(client->priv->bg_box);
		return;
	}
	remmina_ftp_job_describe(job, &percent, &speed, &left);
	text = speed[0] ? g_strdup_printf("%s: %d %%   %s   %s", remmina_ftp_job_title(job), percent, speed, left) :
	       g_strdup_printf("%s: %d %%", remmina_ftp_job_title(job), percent);
	gtk_label_set_text(GTK_LABEL(client->priv->bg_label), text);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(client->priv->bg_bar), percent / 100.0);
	gtk_widget_show(client->priv->bg_box);
	g_free(text);
	g_free(speed);
	g_free(left);
}

static void client_on_background_show(GtkButton *button, RemminaFTPClient *client)
{
	RemminaFTPJob *job = client_background_job(client);
	JobUI *ui;

	if (!job)
		return;
	ui = job->ui_data;
	ui->background = FALSE;
	if (ui->progress) {
		remmina_ftp_progress_update(ui->progress);
		remmina_ftp_progress_present(ui->progress);
	}
	client_update_background_line(client);
}

static void client_on_background_cancel(GtkButton *button, RemminaFTPClient *client)
{
	RemminaFTPJob *job = client_background_job(client);

	if (job) {
		remmina_ftp_job_set_paused(job, FALSE);
		remmina_ftp_job_cancel(job);
	}
}

static void client_start_job_full(RemminaFTPClient *client, RemminaFTPJob *job, RemminaFTPSide marks_side, OpenMode open,
				  const gchar *open_local, const gchar *open_remote)
{
	RemminaFTPClientClass *klass = REMMINA_FTP_CLIENT_GET_CLASS(client);
	JobUI *ui = g_new0(JobUI, 1);

	if (!klass->submit_job) {
		g_free(ui);
		remmina_ftp_job_unref(job);
		return;
	}
	ui->client = client;
	ui->reload_side = marks_side;
	ui->open = open;
	ui->open_local = g_strdup(open_local);
	ui->open_remote = g_strdup(open_remote);
	job->ui_data = ui;
	job->ui_data_free = job_ui_free;
	job->ui.ask_conflict = client_job_ask_conflict;
	job->ui.ask_error = client_job_ask_error;
	job->ui.progress = client_job_progress;
	job->ui.finished = client_job_finished;
	/* The profile's "overwrite all" / "resume all", unless the job decided already (F3, F4) */
	if ((job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE) &&
	    job->conflict_default == REMMINA_FTP_CONFLICT_ASK) {
		if (client->priv->overwrite_all)
			job->conflict_default = REMMINA_FTP_CONFLICT_OVERWRITE_ALL;
		else if (client->priv->resume_all)
			job->conflict_default = REMMINA_FTP_CONFLICT_RESUME_ALL;
	}

	client->priv->jobs = g_list_append(client->priv->jobs, job);	/* the list keeps the reference */
	ui->progress = remmina_ftp_progress_new(client_toplevel(client), job, G_CALLBACK(client_on_job_background), job);
	klass->submit_job(client, job);
}

static void client_start_job(RemminaFTPClient *client, RemminaFTPJob *job, RemminaFTPSide marks_side)
{
	client_start_job_full(client, job, marks_side, OPEN_NONE, NULL, NULL);
}

/* ---- F4 on a server file: its saved copy goes back ---- */

static void edited_free(EditedFile *ef)
{
	if (ef->check_id)
		g_source_remove(ef->check_id);
	if (ef->monitor) {
		g_file_monitor_cancel(ef->monitor);
		g_object_unref(ef->monitor);
	}
	g_free(ef->local);
	g_free(ef->remote);
	g_free(ef);
}

static gboolean edited_stat(const gchar *path, gint64 *mtime, goffset *size)
{
	GStatBuf st;

	if (g_stat(path, &st) != 0)
		return FALSE;
	*mtime = (gint64)st.st_mtim.tv_sec * G_USEC_PER_SEC + st.st_mtim.tv_nsec / 1000;
	*size = st.st_size;
	return TRUE;
}

static gboolean edited_check(gpointer data)
{
	EditedFile *ef = data;
	RemminaFTPClient *client = ef->client;
	gint64 mtime;
	goffset size;
	gboolean upload;

	ef->check_id = 0;
	if (ef->asking || !edited_stat(ef->local, &mtime, &size) || (mtime == ef->mtime && size == ef->size))
		return G_SOURCE_REMOVE;

	/* This version gets an answer; a later save asks again */
	ef->mtime = mtime;
	ef->size = size;
	ef->asking = TRUE;
	g_object_ref(client);
	upload = remmina_ftp_dialog_upload_changed(client_toplevel(client), remmina_ftp_path_basename(ef->remote));
	ef->asking = FALSE;
	if (client->priv->disposed) {
		/* The tab closed during the question: dispose left this one to us */
		edited_free(ef);
		g_object_unref(client);
		return G_SOURCE_REMOVE;
	}
	if (upload) {
		RemminaFTPJob *job = remmina_ftp_job_new(REMMINA_FTP_JOB_COPY);
		job->src_side = REMMINA_FTP_SIDE_LOCAL;
		remmina_ftp_job_add_source(job, ef->local);
		job->dst_side = REMMINA_FTP_SIDE_REMOTE;
		job->dst_dir = remmina_ftp_path_parent(ef->remote);
		job->conflict_default = REMMINA_FTP_CONFLICT_OVERWRITE_ALL;
		client_start_job_full(client, job, REMMINA_FTP_SIDE_REMOTE, OPEN_UPLOAD, NULL, NULL);
	}
	/* Saved again while the question was open */
	if (edited_stat(ef->local, &mtime, &size) && (mtime != ef->mtime || size != ef->size))
		ef->check_id = g_timeout_add(100, edited_check, ef);
	g_object_unref(client);
	return G_SOURCE_REMOVE;
}

static void edited_on_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer data)
{
	EditedFile *ef = data;

	/* Editors write in steps (or replace the file): look once it has settled */
	if (ef->check_id)
		g_source_remove(ef->check_id);
	ef->check_id = g_timeout_add(700, edited_check, ef);
}

static void client_watch_edited(RemminaFTPClient *client, const gchar *local, const gchar *remote)
{
	EditedFile *ef = g_new0(EditedFile, 1);
	GFile *file = g_file_new_for_path(local);

	ef->client = client;
	ef->local = g_strdup(local);
	ef->remote = g_strdup(remote);
	edited_stat(local, &ef->mtime, &ef->size);
	ef->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
	if (ef->monitor)
		g_signal_connect(ef->monitor, "changed", G_CALLBACK(edited_on_changed), ef);
	g_object_unref(file);
	client->priv->edited = g_list_prepend(client->priv->edited, ef);
}

/* F3 / F4 on the cursor file: this computer's opens directly, the server's as a downloaded copy */
static void client_view_edit(RemminaFTPClient *client, gboolean edit)
{
	RemminaFTPPanel *panel = client->priv->active;
	RemminaFTPEntry *entry;
	GError *error = NULL;
	gchar *path;

	if (!remmina_ftp_panel_get_fs(panel) || !remmina_ftp_panel_get_dir(panel))
		return;
	entry = remmina_ftp_panel_get_cursor_entry(panel);
	if (!entry)
		return;
	if (entry->type == REMMINA_FTP_ENTRY_DIR) {
		remmina_ftp_entry_free(entry);
		return;
	}
	path = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), entry->name);
	if (panel == client->priv->left) {
		if (!(edit ? client_open_editor(client, path, &error) : client_open_default(client, path, &error))) {
			remmina_ftp_dialog_message(client_toplevel(client), error->message);
			g_clear_error(&error);
		}
	} else {
		gchar *dir = client_temp_copy_dir(client);
		if (dir) {
			RemminaFTPJob *job = remmina_ftp_job_new(REMMINA_FTP_JOB_COPY);
			gchar *local = g_build_filename(dir, entry->name, NULL);
			job->src_side = REMMINA_FTP_SIDE_REMOTE;
			remmina_ftp_job_add_source(job, path);
			job->dst_side = REMMINA_FTP_SIDE_LOCAL;
			job->dst_dir = dir;
			job->conflict_default = REMMINA_FTP_CONFLICT_OVERWRITE_ALL;
			client_start_job_full(client, job, REMMINA_FTP_SIDE_REMOTE, edit ? OPEN_EDIT : OPEN_VIEW, local, path);
			g_free(local);
		} else {
			remmina_ftp_dialog_message(client_toplevel(client), _("Could not create a temporary folder for the file."));
		}
	}
	g_free(path);
	remmina_ftp_entry_free(entry);
}

/* ---- Operations ---- */

static RemminaFTPJob *client_job_from_targets(RemminaFTPClient *client, RemminaFTPJobOp op, RemminaFTPPanel *panel,
					      GPtrArray *targets)
{
	RemminaFTPJob *job = remmina_ftp_job_new(op);
	guint i;

	job->src_side = client_side(client, panel);
	for (i = 0; i < targets->len; i++) {
		RemminaFTPEntry *entry = g_ptr_array_index(targets, i);
		gchar *path = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), entry->name);
		remmina_ftp_job_add_source(job, path);
		g_free(path);
	}
	return job;
}

static void client_copy(RemminaFTPClient *client, gboolean move)
{
	RemminaFTPPanel *src = client->priv->active;
	RemminaFTPPanel *dst = client_other(client, src);
	GPtrArray *targets;
	RemminaFTPJob *job;
	gchar *dst_dir, *resolved;

	if (!remmina_ftp_panel_get_fs(src) || !remmina_ftp_panel_get_fs(dst) || !remmina_ftp_panel_get_dir(dst))
		return;
	targets = remmina_ftp_panel_get_targets(src);
	if (!targets)
		return;
	dst_dir = remmina_ftp_dialog_copy(client_toplevel(client), move, targets, remmina_ftp_panel_get_dir(dst));
	if (dst_dir) {
		resolved = client_resolve_dir(dst, dst_dir);
		job = client_job_from_targets(client, move ? REMMINA_FTP_JOB_MOVE : REMMINA_FTP_JOB_COPY, src, targets);
		job->dst_side = client_side(client, dst);
		job->dst_dir = resolved;
		client_start_job(client, job, job->src_side);
		g_free(dst_dir);
	}
	g_ptr_array_unref(targets);
}

static void client_delete(RemminaFTPClient *client)
{
	RemminaFTPPanel *panel = client->priv->active;
	GPtrArray *targets;

	if (!remmina_ftp_panel_get_fs(panel))
		return;
	targets = remmina_ftp_panel_get_targets(panel);
	if (!targets)
		return;
	if (remmina_ftp_dialog_confirm_delete(client_toplevel(client), targets))
		client_start_job(client, client_job_from_targets(client, REMMINA_FTP_JOB_DELETE, panel, targets),
				 client_side(client, panel));
	g_ptr_array_unref(targets);
}

static void client_mkdir(RemminaFTPClient *client)
{
	RemminaFTPPanel *panel = client->priv->active;
	RemminaFTPFs *fs = remmina_ftp_panel_get_fs(panel);
	gchar *name, *path, **parts, *first = NULL;
	GError *error = NULL;
	guint i;

	if (!fs || !remmina_ftp_panel_get_dir(panel))
		return;
	name = remmina_ftp_dialog_mkdir(client_toplevel(client));
	if (!name)
		return;
	/* "a/b/c" creates the three levels, as in Total Commander */
	path = name[0] == '/' ? g_strdup("/") : g_strdup(remmina_ftp_panel_get_dir(panel));
	parts = g_strsplit(name, "/", -1);
	for (i = 0; parts[i]; i++) {
		RemminaFTPEntry entry = { 0 };
		gchar *next;
		if (!parts[i][0])
			continue;
		next = remmina_ftp_path_join(path, parts[i]);
		g_free(path);
		path = next;
		if (!first)
			first = g_strdup(parts[i]);
		if (remmina_ftp_fs_stat(fs, path, &entry, NULL)) {
			g_free(entry.name);
			g_free(entry.owner);
			g_free(entry.group);
			continue;
		}
		if (!remmina_ftp_fs_mkdir(fs, path, &error)) {
			remmina_ftp_dialog_message(client_toplevel(client), error->message);
			g_clear_error(&error);
			break;
		}
	}
	g_strfreev(parts);
	remmina_ftp_panel_reload(panel);
	if (first && name[0] != '/')
		remmina_ftp_panel_select_name(panel, first);
	g_free(first);
	g_free(path);
	g_free(name);
}

static void client_on_panel_renamed(RemminaFTPPanel *panel, const gchar *old_name, const gchar *new_name, gpointer data)
{
	RemminaFTPClient *client = data;
	RemminaFTPFs *fs = remmina_ftp_panel_get_fs(panel);
	gchar *from, *to;
	GError *error = NULL;

	if (!fs || strchr(new_name, '/'))
		return;
	from = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), old_name);
	to = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), new_name);
	if (!remmina_ftp_fs_rename(fs, from, to, &error)) {
		remmina_ftp_dialog_message(client_toplevel(client), error->message);
		g_clear_error(&error);
	}
	remmina_ftp_panel_reload(panel);
	remmina_ftp_panel_select_name(panel, error ? old_name : new_name);
	g_free(from);
	g_free(to);
}

static void client_attributes(RemminaFTPClient *client)
{
	RemminaFTPPanel *panel = client->priv->active;
	RemminaFTPFs *fs = remmina_ftp_panel_get_fs(panel);
	GPtrArray *targets;
	RemminaFTPAttributes attr;
	RemminaFTPEntry *first;
	RemminaFTPEntry info = { 0 };
	gchar *path;
	guint i;

	if (!fs)
		return;
	targets = remmina_ftp_panel_get_targets(panel);
	if (!targets)
		return;
	/* The current mode of the first file; its owner and group when the listing did not name them */
	first = g_ptr_array_index(targets, 0);
	path = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), first->name);
	if (remmina_ftp_fs_stat(fs, path, &info, NULL)) {
		if (!first->owner) {
			first->owner = info.owner;
			info.owner = NULL;
		}
		if (!first->group) {
			first->group = info.group;
			info.group = NULL;
		}
		first->mode = info.mode;
	}
	g_free(info.name);
	g_free(info.owner);
	g_free(info.group);
	g_free(path);

	if (remmina_ftp_dialog_attributes(client_toplevel(client), targets, &attr)) {
		if (attr.recursive) {
			RemminaFTPJob *job = client_job_from_targets(client, REMMINA_FTP_JOB_ATTRIBUTES, panel, targets);
			job->mode_set = attr.mode_set;
			job->mode_clear = attr.mode_clear;
			job->set_mtime = attr.set_mtime;
			job->mtime = attr.mtime;
			job->recursive = TRUE;
			client_start_job(client, job, client_side(client, panel));
		} else {
			for (i = 0; i < targets->len; i++) {
				RemminaFTPEntry *entry = g_ptr_array_index(targets, i);
				guint32 mode = (entry->mode & ~attr.mode_clear) | attr.mode_set;
				GError *error = NULL;

				path = remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel), entry->name);
				if (!entry->is_link && mode != entry->mode && !remmina_ftp_fs_chmod(fs, path, mode, &error)) {
					remmina_ftp_dialog_message(client_toplevel(client), error->message);
					g_clear_error(&error);
				}
				if (!entry->is_link && attr.set_mtime && !remmina_ftp_fs_set_mtime(fs, path, attr.mtime, &error)) {
					remmina_ftp_dialog_message(client_toplevel(client), error->message);
					g_clear_error(&error);
				}
				g_free(path);
			}
			remmina_ftp_panel_reload(panel);
		}
	}
	g_ptr_array_unref(targets);
}

static void client_toggle_hidden(RemminaFTPClient *client)
{
	gboolean show = !remmina_ftp_panel_get_show_hidden(client->priv->left);

	remmina_ftp_panel_set_show_hidden(client->priv->left, show);
	remmina_ftp_panel_set_show_hidden(client->priv->right, show);
}

typedef enum {
	ACTION_RELOAD, ACTION_RENAME, ACTION_VIEW, ACTION_EDIT, ACTION_COPY, ACTION_MOVE, ACTION_MKDIR, ACTION_DELETE,
	ACTION_ATTRIBUTES
} ClientAction;

static void client_run_action(RemminaFTPClient *client, ClientAction action)
{
	RemminaFTPPanel *panel = client->priv->active;

	switch (action) {
	case ACTION_RELOAD:
		remmina_ftp_panel_reload(panel);
		break;
	case ACTION_RENAME:
		remmina_ftp_panel_start_rename(panel);
		break;
	case ACTION_VIEW:
		client_view_edit(client, FALSE);
		break;
	case ACTION_EDIT:
		client_view_edit(client, TRUE);
		break;
	case ACTION_COPY:
		client_copy(client, FALSE);
		break;
	case ACTION_MOVE:
		client_copy(client, TRUE);
		break;
	case ACTION_MKDIR:
		client_mkdir(client);
		break;
	case ACTION_DELETE:
		client_delete(client);
		break;
	case ACTION_ATTRIBUTES:
		client_attributes(client);
		break;
	}
}

/* ---- Panel callbacks ---- */

static void client_on_panel_focused(RemminaFTPPanel *panel, gpointer data)
{
	RemminaFTPClient *client = data;

	client->priv->active = panel;
	remmina_ftp_panel_set_active(panel, TRUE);
	remmina_ftp_panel_set_active(client_other(client, panel), FALSE);
}

static gboolean client_on_panel_key(RemminaFTPPanel *panel, GdkEventKey *event, gpointer data)
{
	RemminaFTPClient *client = data;
	GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
	guint latin = client_latin_keyval(event);

	if ((event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab) && (mods & ~GDK_SHIFT_MASK) == 0) {
		remmina_ftp_panel_grab_focus(client_other(client, panel));
		return TRUE;
	}
	if (mods == 0) {
		switch (event->keyval) {
		case GDK_KEY_F1:
			client_run_action(client, ACTION_RELOAD);
			return TRUE;
		case GDK_KEY_F2:
			client_run_action(client, ACTION_RENAME);
			return TRUE;
		case GDK_KEY_F3:
			client_run_action(client, ACTION_VIEW);
			return TRUE;
		case GDK_KEY_F4:
			client_run_action(client, ACTION_EDIT);
			return TRUE;
		case GDK_KEY_F5:
			client_run_action(client, ACTION_COPY);
			return TRUE;
		case GDK_KEY_F6:
			client_run_action(client, ACTION_MOVE);
			return TRUE;
		case GDK_KEY_F7:
			client_run_action(client, ACTION_MKDIR);
			return TRUE;
		case GDK_KEY_F8:
		case GDK_KEY_Delete:
		case GDK_KEY_KP_Delete:
			client_run_action(client, ACTION_DELETE);
			return TRUE;
		default:
			break;
		}
	}
	if (mods == GDK_MOD1_MASK && latin == GDK_KEY_a) {
		client_run_action(client, ACTION_ATTRIBUTES);
		return TRUE;
	}
	if (mods == GDK_CONTROL_MASK && latin == GDK_KEY_h) {
		client_toggle_hidden(client);
		return TRUE;
	}
	return FALSE;
}

static gchar *client_on_panel_ask_mask(RemminaFTPPanel *panel, gboolean mark, gpointer data)
{
	return remmina_ftp_dialog_mask(client_toplevel(data), mark);
}

static void client_on_panel_error(RemminaFTPPanel *panel, const gchar *message, gpointer data)
{
	remmina_ftp_dialog_message(client_toplevel(data), message);
}

typedef struct {
	RemminaFTPClient *	client;
	gchar *			message;
} LateMessage;

/* Not during the drag: the drop must finish first */
static gboolean client_late_message(gpointer data)
{
	LateMessage *lm = data;

	if (!lm->client->priv->disposed)
		remmina_ftp_dialog_message(client_toplevel(lm->client), lm->message);
	g_object_unref(lm->client);
	g_free(lm->message);
	g_free(lm);
	return G_SOURCE_REMOVE;
}

/* Server files dragged out to another program: lent as local paths */
static gchar **client_on_panel_export_uris(RemminaFTPPanel *panel, GPtrArray *targets, gpointer data)
{
	RemminaFTPClient *client = data;
	RemminaFTPClientClass *klass = REMMINA_FTP_CLIENT_GET_CLASS(client);
	GPtrArray *paths;
	GError *error = NULL;
	gchar **uris;
	guint i;

	if (!klass->export_remote || !remmina_ftp_panel_get_dir(panel))
		return NULL;
	paths = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < targets->len; i++)
		g_ptr_array_add(paths, remmina_ftp_path_join(remmina_ftp_panel_get_dir(panel),
							     ((RemminaFTPEntry *)g_ptr_array_index(targets, i))->name));
	uris = klass->export_remote(client, paths, &error);
	g_ptr_array_unref(paths);
	if (!uris) {
		LateMessage *lm = g_new0(LateMessage, 1);
		lm->client = g_object_ref(client);
		lm->message = g_strdup(error ? error->message : _("Could not lend the files to other programs."));
		g_idle_add(client_late_message, lm);
		g_clear_error(&error);
	}
	return uris;
}

static void client_on_panel_drag_begin(RemminaFTPPanel *panel, gpointer data)
{
	RemminaFTPClientClass *klass = REMMINA_FTP_CLIENT_GET_CLASS(data);

	if (klass->prepare_export)
		klass->prepare_export(REMMINA_FTP_CLIENT(data));
}

/* Something was dropped on a panel: copy (or move, between our panels) into its folder */
static void client_on_panel_dropped(RemminaFTPPanel *panel, gchar **uris, GdkDragAction action, gpointer data)
{
	RemminaFTPClient *client = data;
	gboolean move = action == GDK_ACTION_MOVE;
	GPtrArray *targets = NULL;
	RemminaFTPJob *job;
	RemminaFTPSide marks_side;
	gchar *dst_dir;
	guint i;

	if (!remmina_ftp_panel_get_fs(panel) || !remmina_ftp_panel_get_dir(panel))
		return;

	if (!uris) {
		/* The server panel's files, dropped on this computer's panel */
		RemminaFTPPanel *src = client_other(client, panel);
		if (!remmina_ftp_panel_get_fs(src) || !(targets = remmina_ftp_panel_get_targets(src)))
			return;
		job = client_job_from_targets(client, move ? REMMINA_FTP_JOB_MOVE : REMMINA_FTP_JOB_COPY, src, targets);
		marks_side = job->src_side;
	} else {
		/* Local files: from this computer's panel or from another program */
		job = remmina_ftp_job_new(move ? REMMINA_FTP_JOB_MOVE : REMMINA_FTP_JOB_COPY);
		job->src_side = REMMINA_FTP_SIDE_LOCAL;
		marks_side = REMMINA_FTP_SIDE_LOCAL;
		targets = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_ftp_entry_free);
		for (i = 0; uris[i]; i++) {
			gchar *path = g_filename_from_uri(uris[i], NULL, NULL);
			RemminaFTPEntry *entry;
			if (!path)
				continue;
			remmina_ftp_job_add_source(job, path);
			entry = remmina_ftp_entry_new();
			entry->name = g_strdup(remmina_ftp_path_basename(path));
			g_ptr_array_add(targets, entry);
			g_free(path);
		}
		if (targets->len == 0) {
			g_ptr_array_unref(targets);
			remmina_ftp_job_unref(job);
			return;
		}
	}

	/* As in Total Commander, a drop asks where, like F5 */
	dst_dir = remmina_ftp_dialog_copy(client_toplevel(client), move, targets, remmina_ftp_panel_get_dir(panel));
	if (dst_dir) {
		job->dst_side = client_side(client, panel);
		job->dst_dir = client_resolve_dir(panel, dst_dir);
		client_start_job(client, job, marks_side);
		g_free(dst_dir);
	} else {
		remmina_ftp_job_unref(job);
	}
	g_ptr_array_unref(targets);
}

/* ---- The function key bar ---- */

static void client_on_fkey_clicked(GtkButton *button, RemminaFTPClient *client)
{
	client_run_action(client, (ClientAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action")));
	if (client->priv->active)
		remmina_ftp_panel_grab_focus(client->priv->active);
}

static GtkWidget *client_create_fkeys(RemminaFTPClient *client)
{
	static const struct {
		const gchar *	label;
		ClientAction	action;
	} keys[] = {
		/* TRANSLATORS: the function key bar of the SFTP tab, as in Total Commander */
		{ N_("F1 Reread"), ACTION_RELOAD },
		{ N_("F2 Rename"), ACTION_RENAME },
		{ N_("F3 View"), ACTION_VIEW },
		{ N_("F4 Edit"), ACTION_EDIT },
		{ N_("F5 Copy"), ACTION_COPY },
		{ N_("F6 Move"), ACTION_MOVE },
		{ N_("F7 NewFolder"), ACTION_MKDIR },
		{ N_("F8 Delete"), ACTION_DELETE },
		{ N_("Alt+A Attributes"), ACTION_ATTRIBUTES },
	};
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	guint i;

	gtk_box_set_homogeneous(GTK_BOX(bar), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(bar), "unidesk-fm-fkeys");
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		GtkWidget *button = gtk_button_new_with_label(_(keys[i].label));
		/* Equal widths, as in Total Commander: a narrow window shortens the labels, never widens itself */
		gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(button))), PANGO_ELLIPSIZE_END);
		gtk_widget_set_tooltip_text(button, _(keys[i].label));
		gtk_widget_set_focus_on_click(button, FALSE);
		gtk_widget_set_can_focus(button, FALSE);
		g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(keys[i].action));
		g_signal_connect(button, "clicked", G_CALLBACK(client_on_fkey_clicked), client);
		gtk_box_pack_start(GTK_BOX(bar), button, TRUE, TRUE, 0);
	}
	return bar;
}

/* ---- The widget ---- */

static void client_load_css(void)
{
	static gboolean loaded = FALSE;
	GtkCssProvider *provider;
	GBytes *css;

	if (loaded)
		return;
	loaded = TRUE;
	/* Read the bytes directly: gtk_css_provider_load_from_resource() folds "src/../data" (remmina_theme.c) */
	css = g_resources_lookup_data("/org/unidesk/Unidesk/src/../data/style/unidesk-fm.css", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	if (!css)
		return;
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, g_bytes_get_data(css, NULL), (gssize)g_bytes_get_size(css), NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
						  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	g_bytes_unref(css);
}

/* The two panels share the width equally until the user moves the divider */
static void client_on_paned_size_allocate(GtkWidget *widget, GdkRectangle *allocation, RemminaFTPClient *client)
{
	if (client->priv->paned_placed || allocation->width < 20)
		return;
	client->priv->paned_placed = TRUE;
	gtk_paned_set_position(GTK_PANED(widget), allocation->width / 2);
}

static void remmina_ftp_client_dispose(GObject *object)
{
	RemminaFTPClient *client = REMMINA_FTP_CLIENT(object);

	if (client->priv && !client->priv->disposed) {
		GList *l;

		client->priv->disposed = TRUE;
		remmina_ftp_client_cancel_jobs(client);
		remmina_ftp_fs_free(client->priv->local_fs);
		client->priv->local_fs = NULL;
		/* F4 copies: no more uploads; a copy whose question is open is freed when it is answered */
		for (l = client->priv->edited; l; l = l->next) {
			EditedFile *ef = l->data;
			if (!ef->asking)
				edited_free(ef);
		}
		g_list_free(client->priv->edited);
		client->priv->edited = NULL;
		if (client->priv->temp_dir) {
			client_remove_tree(client->priv->temp_dir);
			g_clear_pointer(&client->priv->temp_dir, g_free);
		}
	}
	G_OBJECT_CLASS(remmina_ftp_client_parent_class)->dispose(object);
}

static void remmina_ftp_client_finalize(GObject *object)
{
	RemminaFTPClient *client = REMMINA_FTP_CLIENT(object);

	g_free(client->priv->start_remote);
	g_free(client->priv);
	G_OBJECT_CLASS(remmina_ftp_client_parent_class)->finalize(object);
}

static void remmina_ftp_client_class_init(RemminaFTPClientClass *klass)
{
	TRACE_CALL(__func__);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = remmina_ftp_client_dispose;
	object_class->finalize = remmina_ftp_client_finalize;
	klass->submit_job = NULL;
	klass->export_remote = NULL;
	klass->prepare_export = NULL;
}

static void remmina_ftp_client_init(RemminaFTPClient *client)
{
	TRACE_CALL(__func__);
	RemminaFTPClientPriv *priv = g_new0(RemminaFTPClientPriv, 1);
	RemminaFTPPanelCallbacks cb = {
		.focused = client_on_panel_focused,
		.key = client_on_panel_key,
		.dropped = client_on_panel_dropped,
		.renamed = client_on_panel_renamed,
		.ask_mask = client_on_panel_ask_mask,
		.error = client_on_panel_error,
		.export_uris = client_on_panel_export_uris,
		.drag_begin = client_on_panel_drag_begin,
		.data = client,
	};
	GtkWidget *w;

	client->priv = priv;
	client_load_css();

	gtk_orientable_set_orientation(GTK_ORIENTABLE(client), GTK_ORIENTATION_VERTICAL);
	gtk_widget_set_hexpand(GTK_WIDGET(client), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(client), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(client)), "unidesk-fm");

	priv->local_fs = remmina_ftp_fs_new_local();
	priv->left = remmina_ftp_panel_new(TRUE, &cb);
	priv->right = remmina_ftp_panel_new(FALSE, &cb);
	priv->active = priv->left;

	priv->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_wide_handle(GTK_PANED(priv->paned), TRUE);
	gtk_paned_pack1(GTK_PANED(priv->paned), remmina_ftp_panel_get_widget(priv->left), TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(priv->paned), remmina_ftp_panel_get_widget(priv->right), TRUE, FALSE);
	g_signal_connect(priv->paned, "size-allocate", G_CALLBACK(client_on_paned_size_allocate), client);
	gtk_box_pack_start(GTK_BOX(client), priv->paned, TRUE, TRUE, 0);

	/* A job sent to the background: its progress, a button to bring its window back, and Cancel */
	priv->bg_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(priv->bg_box), "unidesk-fm-background");
	priv->bg_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(priv->bg_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(priv->bg_label), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(priv->bg_box), priv->bg_label, TRUE, TRUE, 0);
	priv->bg_bar = gtk_progress_bar_new();
	gtk_widget_set_valign(priv->bg_bar, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(priv->bg_bar, 200, -1);
	gtk_box_pack_start(GTK_BOX(priv->bg_box), priv->bg_bar, FALSE, FALSE, 0);
	w = gtk_button_new_with_mnemonic(_("_Show"));
	gtk_widget_set_focus_on_click(w, FALSE);
	g_signal_connect(w, "clicked", G_CALLBACK(client_on_background_show), client);
	gtk_box_pack_start(GTK_BOX(priv->bg_box), w, FALSE, FALSE, 0);
	w = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_widget_set_focus_on_click(w, FALSE);
	g_signal_connect(w, "clicked", G_CALLBACK(client_on_background_cancel), client);
	gtk_box_pack_start(GTK_BOX(priv->bg_box), w, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(client), priv->bg_box, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(client), client_create_fkeys(client), FALSE, FALSE, 0);

	gtk_widget_show_all(GTK_WIDGET(client));
	gtk_widget_hide(priv->bg_box);

	/* This computer, from the root */
	remmina_ftp_panel_set_title(priv->left, _("Local computer"));
	remmina_ftp_panel_set_title(priv->right, "SFTP");
	remmina_ftp_panel_set_fs(priv->left, priv->local_fs);
	remmina_ftp_panel_open(priv->left, "/");
	remmina_ftp_panel_set_message(priv->right, _("Connecting to the server…"));
	remmina_ftp_panel_set_active(priv->left, TRUE);
}

GtkWidget *remmina_ftp_client_new(void)
{
	TRACE_CALL(__func__);
	return GTK_WIDGET(g_object_new(REMMINA_TYPE_FTP_CLIENT, NULL));
}

void remmina_ftp_client_save_state(RemminaFTPClient *client, RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
	/* Both panels always open at the root, nothing to keep */
}

void remmina_ftp_client_load_state(RemminaFTPClient *client, RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
}

void remmina_ftp_client_set_server_name(RemminaFTPClient *client, const gchar *server)
{
	gchar *title;

	TRACE_CALL(__func__);
	/* TRANSLATORS: the line on top of the server panel, %s is the server address */
	title = server && server[0] ? g_strdup_printf(_("SFTP: %s"), server) : g_strdup("SFTP");
	remmina_ftp_panel_set_title(client->priv->right, title);
	g_free(title);
}

void remmina_ftp_client_set_show_hidden(RemminaFTPClient *client, gboolean show_hidden)
{
	TRACE_CALL(__func__);
	remmina_ftp_panel_set_show_hidden(client->priv->left, show_hidden);
	remmina_ftp_panel_set_show_hidden(client->priv->right, show_hidden);
}

void remmina_ftp_client_set_overwrite_status(RemminaFTPClient *client, gboolean status)
{
	client->priv->overwrite_all = status;
}

gboolean remmina_ftp_client_get_overwrite_status(RemminaFTPClient *client)
{
	return client->priv->overwrite_all;
}

void remmina_ftp_client_set_resume_status(RemminaFTPClient *client, gboolean status)
{
	client->priv->resume_all = status;
}

gboolean remmina_ftp_client_get_resume_status(RemminaFTPClient *client)
{
	return client->priv->resume_all;
}

void remmina_ftp_client_set_remote_fs(RemminaFTPClient *client, RemminaFTPFs *fs)
{
	TRACE_CALL(__func__);
	RemminaFTPPanel *right = client->priv->right;

	remmina_ftp_panel_set_fs(right, fs);
	if (fs) {
		remmina_ftp_panel_set_message(right, NULL);
		/* A restored tab opens where it was, if that folder is still there */
		remmina_ftp_panel_open(right, client_folder_exists(fs, client->priv->start_remote) ? client->priv->start_remote : "/");
		g_clear_pointer(&client->priv->start_remote, g_free);
	} else {
		remmina_ftp_panel_set_message(right, _("Not connected"));
	}
}

void remmina_ftp_client_get_dirs(RemminaFTPClient *client, const gchar **local_dir, const gchar **remote_dir)
{
	if (local_dir)
		*local_dir = remmina_ftp_panel_get_dir(client->priv->left);
	if (remote_dir)
		*remote_dir = remmina_ftp_panel_get_fs(client->priv->right) ? remmina_ftp_panel_get_dir(client->priv->right) : NULL;
}

void remmina_ftp_client_set_start_dirs(RemminaFTPClient *client, const gchar *local_dir, const gchar *remote_dir)
{
	if (client_folder_exists(client->priv->local_fs, local_dir))
		remmina_ftp_panel_open(client->priv->left, local_dir);
	g_free(client->priv->start_remote);
	client->priv->start_remote = g_strdup(remote_dir);
}

void remmina_ftp_client_cancel_jobs(RemminaFTPClient *client)
{
	GList *l;

	for (l = client->priv->jobs; l; l = l->next) {
		RemminaFTPJob *job = l->data;
		JobUI *ui = job->ui_data;

		ui->client = NULL;
		if (ui->progress) {
			remmina_ftp_progress_free(ui->progress);
			ui->progress = NULL;
		}
		remmina_ftp_job_set_paused(job, FALSE);
		remmina_ftp_job_cancel(job);
		remmina_ftp_job_unref(job);
	}
	g_list_free(client->priv->jobs);
	client->priv->jobs = NULL;
}
