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

#ifdef HAVE_LIBSSH

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <pthread.h>
#include <string.h>
#include "remmina_public.h"
#include "remmina_log.h"
#include "remmina_ssh.h"
#include "remmina_sftp_client.h"
#include "remmina_sftp_plugin.h"
#ifdef HAVE_FUSE3
#include "remmina_ftp_fuse.h"
#endif
#include "remmina/remmina_trace_calls.h"

G_DEFINE_TYPE(RemminaSFTPClient, remmina_sftp_client, REMMINA_TYPE_FTP_CLIENT)

/* ---- The transfer thread ---- */

/* A session of its own for the thread, on the same server and credentials (as before the two panels) */
static RemminaSFTP *remmina_sftp_client_thread_session(RemminaSFTPClient *client, gchar **error)
{
	RemminaSFTP *sftp = remmina_sftp_new_from_ssh(REMMINA_SSH(client->sftp));
	gchar *host = NULL;
	int port = 0;

	if (!remmina_plugin_sftp_start_direct_tunnel(client->gp, &host, &port)) {
		*error = g_strdup(_("Could not open the tunnel to the server."));
		remmina_sftp_free(sftp);
		return NULL;
	}
	REMMINA_SSH(sftp)->tunnel_entrance_host = host;
	REMMINA_SSH(sftp)->tunnel_entrance_port = port;

	if (!remmina_ssh_init_session(REMMINA_SSH(sftp)) ||
	    remmina_ssh_auth(REMMINA_SSH(sftp), REMMINA_SSH(sftp)->password, client->gp, NULL) != REMMINA_SSH_AUTH_SUCCESS ||
	    !remmina_sftp_open(sftp)) {
		*error = g_strdup(REMMINA_SSH(sftp)->error ? REMMINA_SSH(sftp)->error : _("Could not connect to the server."));
		remmina_sftp_free(sftp);
		return NULL;
	}
	return sftp;
}

static gpointer remmina_sftp_client_thread_main(gpointer data)
{
	TRACE_CALL(__func__);
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(data);
	RemminaFTPFs *local = remmina_ftp_fs_new_local();
	RemminaFTPFs *remote = NULL;
	RemminaSFTP *sftp = NULL;
	RemminaFTPJob *job;

	for (;;) {
		g_mutex_lock(&client->queue_lock);
		job = client->thread_abort ? NULL : g_queue_pop_head(client->queue);
		client->current = job;
		if (!job)
			client->thread_running = FALSE;
		g_mutex_unlock(&client->queue_lock);
		if (!job)
			break;

		if (remmina_ftp_job_needs_remote(job) && !remote) {
			gchar *error = NULL;
			sftp = remmina_sftp_client_thread_session(client, &error);
			if (sftp) {
				remote = remmina_ftp_fs_new_remote(sftp);
			} else {
				if (job->ui.ask_error)
					job->ui.ask_error(job, error);
				remmina_ftp_job_cancel(job);
				g_free(error);
			}
		}

		remmina_ftp_job_run(job, local, remote);

		g_mutex_lock(&client->queue_lock);
		client->current = NULL;
		g_mutex_unlock(&client->queue_lock);
		remmina_ftp_job_unref(job);
	}

	remmina_ftp_fs_free(remote);
	if (sftp)
		remmina_sftp_free(sftp);
	remmina_ftp_fs_free(local);
	return NULL;
}

static void remmina_sftp_client_submit_job(RemminaFTPClient *ftp_client, RemminaFTPJob *job)
{
	TRACE_CALL(__func__);
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(ftp_client);
	gboolean start;
	pthread_t thread;

	g_mutex_lock(&client->queue_lock);
	g_queue_push_tail(client->queue, remmina_ftp_job_ref(job));
	start = !client->thread_running;
	if (start)
		client->thread_running = TRUE;
	g_mutex_unlock(&client->queue_lock);

	if (start) {
		if (pthread_create(&thread, NULL, remmina_sftp_client_thread_main, client) == 0) {
			pthread_detach(thread);
		} else {
			g_mutex_lock(&client->queue_lock);
			client->thread_running = FALSE;
			g_mutex_unlock(&client->queue_lock);
			remmina_ftp_job_cancel(job);
		}
	}
}

/* ---- Files dragged out to other programs ---- */

#ifdef HAVE_FUSE3
/* In the FUSE thread: a session of its own, like the transfer thread's */
static RemminaFTPFs *remmina_sftp_client_fuse_open(gpointer data, gpointer *session, gchar **error)
{
	RemminaSFTP *sftp = remmina_sftp_client_thread_session(REMMINA_SFTP_CLIENT(data), error);

	if (!sftp)
		return NULL;
	*session = sftp;
	return remmina_ftp_fs_new_remote(sftp);
}

static void remmina_sftp_client_fuse_close(RemminaFTPFs *fs, gpointer session)
{
	remmina_ftp_fs_free(fs);
	remmina_sftp_free(session);
}

static RemminaFTPFuse *remmina_sftp_client_get_fuse(RemminaSFTPClient *client)
{
	if (!client->fuse)
		client->fuse = remmina_ftp_fuse_new(remmina_sftp_client_fuse_open, remmina_sftp_client_fuse_close, client);
	return client->fuse;
}

static gchar **remmina_sftp_client_export_remote(RemminaFTPClient *ftp_client, GPtrArray *remote_paths, GError **error)
{
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(ftp_client);

	if (!client->sftp) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, _("Not connected to the server."));
		return NULL;
	}
	return remmina_ftp_fuse_export(remmina_sftp_client_get_fuse(client), remote_paths, error);
}

/* The drag has begun: mount and connect now, the drop comes in a moment */
static void remmina_sftp_client_prepare_export(RemminaFTPClient *ftp_client)
{
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(ftp_client);

	if (client->sftp)
		remmina_ftp_fuse_start(remmina_sftp_client_get_fuse(client), NULL);
}
#endif

/* ---- The server panel ---- */

static gboolean remmina_sftp_client_open_idle(gpointer data)
{
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(data);

	if (client->sftp && !client->remote_fs) {
		RemminaSSH *ssh = REMMINA_SSH(client->sftp);
		gchar *server;

		if (!ssh->port || ssh->port == 22)
			server = g_strdup(ssh->server);
		else if (strchr(ssh->server, ':'))	/* IPv6 */
			server = g_strdup_printf("[%s]:%d", ssh->server, ssh->port);
		else
			server = g_strdup_printf("%s:%d", ssh->server, ssh->port);

		remmina_ftp_client_set_server_name(REMMINA_FTP_CLIENT(client), server);
		g_free(server);
		/* A tab restored from a session opens its panels where they were (rcw.c sets these) */
		if (client->gp)
			remmina_ftp_client_set_start_dirs(REMMINA_FTP_CLIENT(client),
							  g_object_get_data(G_OBJECT(client->gp), "unidesk-restore-left"),
							  g_object_get_data(G_OBJECT(client->gp), "unidesk-restore-right"));
		client->remote_fs = remmina_ftp_fs_new_remote(client->sftp);
		remmina_ftp_client_set_remote_fs(REMMINA_FTP_CLIENT(client), client->remote_fs);
	}
	g_object_unref(client);
	return G_SOURCE_REMOVE;
}

void remmina_sftp_client_open(RemminaSFTPClient *client, RemminaSFTP *sftp)
{
	TRACE_CALL(__func__);
	client->sftp = sftp;
	/* Called from the connection thread: the panel is filled in the main thread */
	g_idle_add(remmina_sftp_client_open_idle, g_object_ref(client));
}

/* ---- The object ---- */

static void remmina_sftp_client_dispose(GObject *object)
{
	TRACE_CALL(__func__);
	RemminaSFTPClient *client = REMMINA_SFTP_CLIENT(object);
	gboolean running;

#ifdef HAVE_FUSE3
	/* Its thread connects with this object: first */
	remmina_ftp_fuse_free(client->fuse);
	client->fuse = NULL;
#endif
	if (client->queue) {
		/* Stop the jobs, and wait for the thread: it uses this object */
		remmina_ftp_client_cancel_jobs(REMMINA_FTP_CLIENT(client));
		g_mutex_lock(&client->queue_lock);
		client->thread_abort = TRUE;
		while (!g_queue_is_empty(client->queue))
			remmina_ftp_job_unref(g_queue_pop_head(client->queue));
		if (client->current)
			remmina_ftp_job_cancel(client->current);
		g_mutex_unlock(&client->queue_lock);
		do {
			/* The thread may wait for an answer from the main loop: let it get "cancel" */
			g_main_context_iteration(NULL, FALSE);
			g_mutex_lock(&client->queue_lock);
			running = client->thread_running;
			g_mutex_unlock(&client->queue_lock);
			if (running)
				g_usleep(10000);
		} while (running);
		g_queue_free(client->queue);
		client->queue = NULL;

		remmina_ftp_client_set_remote_fs(REMMINA_FTP_CLIENT(client), NULL);
		remmina_ftp_fs_free(client->remote_fs);
		client->remote_fs = NULL;
		if (client->sftp) {
			remmina_sftp_free(client->sftp);
			client->sftp = NULL;
		}
		g_mutex_clear(&client->queue_lock);
	}
	G_OBJECT_CLASS(remmina_sftp_client_parent_class)->dispose(object);
}

static void remmina_sftp_client_class_init(RemminaSFTPClientClass *klass)
{
	TRACE_CALL(__func__);
	G_OBJECT_CLASS(klass)->dispose = remmina_sftp_client_dispose;
	REMMINA_FTP_CLIENT_CLASS(klass)->submit_job = remmina_sftp_client_submit_job;
#ifdef HAVE_FUSE3
	REMMINA_FTP_CLIENT_CLASS(klass)->export_remote = remmina_sftp_client_export_remote;
	REMMINA_FTP_CLIENT_CLASS(klass)->prepare_export = remmina_sftp_client_prepare_export;
	remmina_ftp_fuse_unmount_leftovers_at_start();
#endif
}

static void remmina_sftp_client_init(RemminaSFTPClient *client)
{
	TRACE_CALL(__func__);
	client->sftp = NULL;
	client->remote_fs = NULL;
	g_mutex_init(&client->queue_lock);
	client->queue = g_queue_new();
	client->current = NULL;
	client->thread_running = FALSE;
	client->thread_abort = FALSE;
}

RemminaSFTPClient *remmina_sftp_client_new(void)
{
	TRACE_CALL(__func__);
	return REMMINA_SFTP_CLIENT(g_object_new(REMMINA_TYPE_SFTP_CLIENT, NULL));
}

#endif
