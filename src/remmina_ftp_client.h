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

/**
 * @file remmina_ftp_client.h
 * @brief The SFTP tab: two panels as in Total Commander, this computer on the left and the
 * server on the right, the function key bar below and the line of a job in the background.
 *
 * RemminaSFTPClient derives from it: it opens the server side (remmina_ftp_client_set_remote_fs)
 * and runs the jobs in its transfer thread (RemminaFTPClientClass::submit_job).
 */

#pragma once

#include <gtk/gtk.h>
#include "remmina_file.h"
#include "remmina_ftp_fs.h"
#include "remmina_ftp_job.h"

G_BEGIN_DECLS

#define REMMINA_TYPE_FTP_CLIENT               (remmina_ftp_client_get_type())
#define REMMINA_FTP_CLIENT(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj), REMMINA_TYPE_FTP_CLIENT, RemminaFTPClient))
#define REMMINA_FTP_CLIENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), REMMINA_TYPE_FTP_CLIENT, RemminaFTPClientClass))
#define REMMINA_IS_FTP_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), REMMINA_TYPE_FTP_CLIENT))
#define REMMINA_IS_FTP_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass), REMMINA_TYPE_FTP_CLIENT))
#define REMMINA_FTP_CLIENT_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS((obj), REMMINA_TYPE_FTP_CLIENT, RemminaFTPClientClass))

typedef struct _RemminaFTPClientPriv RemminaFTPClientPriv;

typedef struct _RemminaFTPClient {
	GtkBox			vbox;

	RemminaFTPClientPriv *	priv;
} RemminaFTPClient;

typedef struct _RemminaFTPClientClass {
	GtkBoxClass parent_class;

	/* Queue the job in the transfer thread, which keeps a reference until it is done */
	void (*submit_job)(RemminaFTPClient *client, RemminaFTPJob *job);
	/* Server files dragged out to another program: file:// URIs it can copy from, NULL with *error set */
	gchar **(*export_remote)(RemminaFTPClient *client, GPtrArray *remote_paths, GError **error);
	/* A drag out of the server panel begins: get ready for export_remote (may be NULL) */
	void (*prepare_export)(RemminaFTPClient *client);
} RemminaFTPClientClass;

GType remmina_ftp_client_get_type(void) G_GNUC_CONST;

GtkWidget *remmina_ftp_client_new(void);

void remmina_ftp_client_save_state(RemminaFTPClient *client, RemminaFile *remminafile);
void remmina_ftp_client_load_state(RemminaFTPClient *client, RemminaFile *remminafile);

void remmina_ftp_client_set_show_hidden(RemminaFTPClient *client, gboolean show_hidden);
/* The defaults of the "file exists" question (profile: overwrite_all, resume_all) */
void remmina_ftp_client_set_overwrite_status(RemminaFTPClient *client, gboolean status);
gboolean remmina_ftp_client_get_overwrite_status(RemminaFTPClient *client);
void remmina_ftp_client_set_resume_status(RemminaFTPClient *client, gboolean status);
gboolean remmina_ftp_client_get_resume_status(RemminaFTPClient *client);

/* The server side is open (the file system is borrowed; NULL: closed). The right panel shows "/". */
void remmina_ftp_client_set_remote_fs(RemminaFTPClient *client, RemminaFTPFs *fs);
/* The server's address for the line on top of its panel ("SFTP: 192.0.2.10") */
void remmina_ftp_client_set_server_name(RemminaFTPClient *client, const gchar *server);
/* Ends the jobs that wait or run (the client is going away) */
void remmina_ftp_client_cancel_jobs(RemminaFTPClient *client);
/* Sessions: the folders the panels show (NULL while unknown), and the ones a restored tab opens at */
void remmina_ftp_client_get_dirs(RemminaFTPClient *client, const gchar **local_dir, const gchar **remote_dir);
void remmina_ftp_client_set_start_dirs(RemminaFTPClient *client, const gchar *local_dir, const gchar *remote_dir);

G_END_DECLS
