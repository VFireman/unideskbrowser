/*
 * Unidesk - copy, move, delete and attribute jobs of the SFTP tab
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
 * @file remmina_ftp_job.h
 * @brief A job runs in the transfer thread and works on both sides through RemminaFTPFs.
 *
 * The user interface creates the job, fills its sources and options and hands it to the
 * transfer thread (RemminaFTPClientClass::submit_job). The thread calls remmina_ftp_job_run(),
 * which reports through RemminaFTPJobUI: progress (must not block) and the questions
 * (conflict, error), which block the thread until the user answers.
 */

#pragma once

#include <glib.h>
#include "remmina_ftp_fs.h"

G_BEGIN_DECLS

typedef enum {
	REMMINA_FTP_JOB_COPY,
	REMMINA_FTP_JOB_MOVE,
	REMMINA_FTP_JOB_DELETE,
	REMMINA_FTP_JOB_ATTRIBUTES,
} RemminaFTPJobOp;

typedef enum {
	REMMINA_FTP_SIDE_LOCAL,
	REMMINA_FTP_SIDE_REMOTE,
} RemminaFTPSide;

typedef enum {
	REMMINA_FTP_CONFLICT_ASK,
	REMMINA_FTP_CONFLICT_OVERWRITE,
	REMMINA_FTP_CONFLICT_OVERWRITE_ALL,
	REMMINA_FTP_CONFLICT_SKIP,
	REMMINA_FTP_CONFLICT_SKIP_ALL,
	REMMINA_FTP_CONFLICT_OVERWRITE_OLDER_ALL,
	REMMINA_FTP_CONFLICT_RENAME,
	REMMINA_FTP_CONFLICT_RESUME,
	REMMINA_FTP_CONFLICT_RESUME_ALL,
	REMMINA_FTP_CONFLICT_CANCEL,
} RemminaFTPConflict;

typedef enum {
	REMMINA_FTP_ERROR_SKIP,
	REMMINA_FTP_ERROR_SKIP_ALL,
	REMMINA_FTP_ERROR_RETRY,
	REMMINA_FTP_ERROR_CANCEL,
} RemminaFTPErrorAnswer;

typedef enum {
	REMMINA_FTP_JOB_QUEUED,
	REMMINA_FTP_JOB_SCANNING,
	REMMINA_FTP_JOB_RUNNING,
	REMMINA_FTP_JOB_DONE,
	REMMINA_FTP_JOB_CANCELLED,
} RemminaFTPJobState;

typedef struct _RemminaFTPJob RemminaFTPJob;

typedef struct {
	/* From the job thread; block until the user answers. new_name: for RENAME */
	RemminaFTPConflict (*ask_conflict)(RemminaFTPJob *job, const gchar *src_path, const RemminaFTPEntry *src,
					   const gchar *dst_path, const RemminaFTPEntry *dst, gchar **new_name);
	RemminaFTPErrorAnswer (*ask_error)(RemminaFTPJob *job, const gchar *message);
	/* From the job thread; must not block: the counters changed, or the job ended */
	void (*progress)(RemminaFTPJob *job);
	void (*finished)(RemminaFTPJob *job);
} RemminaFTPJobUI;

struct _RemminaFTPJob {
	gint			ref;
	RemminaFTPJobOp		op;
	RemminaFTPSide		src_side;
	RemminaFTPSide		dst_side;	/* COPY and MOVE */
	GPtrArray *		sources;	/* absolute paths (gchar *) on src_side */
	gchar *			dst_dir;	/* COPY and MOVE */

	/* ATTRIBUTES: new mode = (old & ~mode_clear) | mode_set */
	guint32			mode_set;
	guint32			mode_clear;
	gboolean		set_mtime;
	gint64			mtime;
	gboolean		recursive;

	/* The sticky answer to a conflict (ASK, or one of the …_ALL answers) */
	RemminaFTPConflict	conflict_default;

	RemminaFTPJobUI		ui;
	gpointer		ui_data;	/* the user interface's own state (windows…) */
	GDestroyNotify		ui_data_free;

	/* Shared between the thread and the user interface: under lock */
	GMutex			lock;
	GCond			cond;
	RemminaFTPJobState	state;
	gboolean		paused;
	gboolean		cancelled;
	guint64			bytes_total;
	guint64			bytes_done;
	guint64			file_total;
	guint64			file_done;
	guint			items_total;
	guint			items_done;
	gchar *			current_src;
	gchar *			current_dst;
	gint64			started_us;	/* g_get_monotonic_time() when the data started to flow */
	gint64			paused_us;	/* time spent paused */
	gint64			pause_started_us;

	/* Thread only */
	gboolean		skip_errors;
	gint64			last_progress_us;
};

RemminaFTPJob *remmina_ftp_job_new(RemminaFTPJobOp op);
RemminaFTPJob *remmina_ftp_job_ref(RemminaFTPJob *job);
void remmina_ftp_job_unref(RemminaFTPJob *job);
void remmina_ftp_job_add_source(RemminaFTPJob *job, const gchar *path);
gboolean remmina_ftp_job_needs_remote(RemminaFTPJob *job);

/* The user interface side, thread safe */
void remmina_ftp_job_set_paused(RemminaFTPJob *job, gboolean paused);
void remmina_ftp_job_cancel(RemminaFTPJob *job);
/* Bytes per second over the time the data flowed, 0 when unknown */
gdouble remmina_ftp_job_speed(RemminaFTPJob *job);

/* The transfer thread: local and remote (remote may be NULL for a local-only job) */
void remmina_ftp_job_run(RemminaFTPJob *job, RemminaFTPFs *local, RemminaFTPFs *remote);

G_END_DECLS
