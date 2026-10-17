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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <string.h>

#include "remmina_ftp_job.h"

#define JOB_BUFFER_SIZE		(256 * 1024)
#define JOB_MAX_DEPTH		64
#define JOB_PROGRESS_INTERVAL	100000	/* µs */

/* One step of the job: a file or a folder, in the order it is processed */
typedef struct {
	gchar *			src;
	gchar *			dst;	/* COPY and MOVE */
	RemminaFTPEntry		entry;	/* the source */
	guint			top;	/* which of job->sources it comes from */
	gboolean		done;
} JobItem;

static void job_item_free(JobItem *item)
{
	g_free(item->src);
	g_free(item->dst);
	g_free(item->entry.name);
	g_free(item->entry.owner);
	g_free(item->entry.group);
	g_free(item);
}

RemminaFTPJob *remmina_ftp_job_new(RemminaFTPJobOp op)
{
	RemminaFTPJob *job = g_new0(RemminaFTPJob, 1);

	job->ref = 1;
	job->op = op;
	job->sources = g_ptr_array_new_with_free_func(g_free);
	job->conflict_default = REMMINA_FTP_CONFLICT_ASK;
	job->state = REMMINA_FTP_JOB_QUEUED;
	g_mutex_init(&job->lock);
	g_cond_init(&job->cond);
	return job;
}

RemminaFTPJob *remmina_ftp_job_ref(RemminaFTPJob *job)
{
	g_atomic_int_inc(&job->ref);
	return job;
}

void remmina_ftp_job_unref(RemminaFTPJob *job)
{
	if (!job || !g_atomic_int_dec_and_test(&job->ref))
		return;
	if (job->ui_data && job->ui_data_free)
		job->ui_data_free(job->ui_data);
	g_ptr_array_unref(job->sources);
	g_free(job->dst_dir);
	g_free(job->current_src);
	g_free(job->current_dst);
	g_mutex_clear(&job->lock);
	g_cond_clear(&job->cond);
	g_free(job);
}

void remmina_ftp_job_add_source(RemminaFTPJob *job, const gchar *path)
{
	g_ptr_array_add(job->sources, g_strdup(path));
}

gboolean remmina_ftp_job_needs_remote(RemminaFTPJob *job)
{
	if (job->src_side == REMMINA_FTP_SIDE_REMOTE)
		return TRUE;
	return (job->op == REMMINA_FTP_JOB_COPY || job->op == REMMINA_FTP_JOB_MOVE) &&
	       job->dst_side == REMMINA_FTP_SIDE_REMOTE;
}

void remmina_ftp_job_set_paused(RemminaFTPJob *job, gboolean paused)
{
	g_mutex_lock(&job->lock);
	if (paused && !job->paused) {
		job->pause_started_us = g_get_monotonic_time();
	} else if (!paused && job->paused && job->pause_started_us) {
		job->paused_us += g_get_monotonic_time() - job->pause_started_us;
		job->pause_started_us = 0;
	}
	job->paused = paused;
	g_cond_broadcast(&job->cond);
	g_mutex_unlock(&job->lock);
}

void remmina_ftp_job_cancel(RemminaFTPJob *job)
{
	g_mutex_lock(&job->lock);
	job->cancelled = TRUE;
	g_cond_broadcast(&job->cond);
	g_mutex_unlock(&job->lock);
}

gdouble remmina_ftp_job_speed(RemminaFTPJob *job)
{
	gdouble speed = 0;
	gint64 now = g_get_monotonic_time();
	gint64 elapsed;

	g_mutex_lock(&job->lock);
	if (job->started_us) {
		elapsed = now - job->started_us - job->paused_us;
		if (job->paused && job->pause_started_us)
			elapsed -= now - job->pause_started_us;
		if (elapsed > 200000)
			speed = (gdouble)job->bytes_done * G_USEC_PER_SEC / (gdouble)elapsed;
	}
	g_mutex_unlock(&job->lock);
	return speed;
}

/* ---- Thread side helpers ---- */

static gboolean job_is_cancelled(RemminaFTPJob *job)
{
	gboolean cancelled;

	g_mutex_lock(&job->lock);
	cancelled = job->cancelled;
	g_mutex_unlock(&job->lock);
	return cancelled;
}

/* Waits while paused; FALSE once the job is cancelled */
static gboolean job_wait(RemminaFTPJob *job)
{
	gboolean go;

	g_mutex_lock(&job->lock);
	while (job->paused && !job->cancelled)
		g_cond_wait(&job->cond, &job->lock);
	go = !job->cancelled;
	g_mutex_unlock(&job->lock);
	return go;
}

static void job_progress(RemminaFTPJob *job, gboolean force)
{
	gint64 now = g_get_monotonic_time();

	if (!force && now - job->last_progress_us < JOB_PROGRESS_INTERVAL)
		return;
	job->last_progress_us = now;
	if (job->ui.progress)
		job->ui.progress(job);
}

static void job_set_current(RemminaFTPJob *job, const gchar *src, const gchar *dst, guint64 file_total)
{
	g_mutex_lock(&job->lock);
	g_free(job->current_src);
	g_free(job->current_dst);
	job->current_src = g_strdup(src);
	job->current_dst = g_strdup(dst);
	job->file_total = file_total;
	job->file_done = 0;
	g_mutex_unlock(&job->lock);
	job_progress(job, TRUE);
}

static void job_add_bytes(RemminaFTPJob *job, guint64 n)
{
	g_mutex_lock(&job->lock);
	job->file_done += n;
	job->bytes_done += n;
	g_mutex_unlock(&job->lock);
}

/* A retried file starts again from zero */
static void job_rewind_file(RemminaFTPJob *job)
{
	g_mutex_lock(&job->lock);
	job->bytes_done -= MIN(job->bytes_done, job->file_done);
	job->file_done = 0;
	g_mutex_unlock(&job->lock);
}

static void job_item_done(RemminaFTPJob *job)
{
	g_mutex_lock(&job->lock);
	job->items_done++;
	g_mutex_unlock(&job->lock);
	job_progress(job, FALSE);
}

/* An error with a file: TRUE to try the step again; FALSE to skip it (or the job was cancelled) */
static gboolean job_error(RemminaFTPJob *job, GError **error)
{
	RemminaFTPErrorAnswer answer = REMMINA_FTP_ERROR_CANCEL;

	if (job->skip_errors || job_is_cancelled(job)) {
		g_clear_error(error);
		return FALSE;
	}
	if (job->ui.ask_error)
		answer = job->ui.ask_error(job, *error ? (*error)->message : _("Unknown error"));
	g_clear_error(error);

	switch (answer) {
	case REMMINA_FTP_ERROR_RETRY:
		return TRUE;
	case REMMINA_FTP_ERROR_SKIP_ALL:
		job->skip_errors = TRUE;
		return FALSE;
	case REMMINA_FTP_ERROR_SKIP:
		return FALSE;
	case REMMINA_FTP_ERROR_CANCEL:
	default:
		remmina_ftp_job_cancel(job);
		return FALSE;
	}
}

/* ---- Planning ---- */

/* pre-order for copying and attributes (a folder before what it holds), post-order for deleting */
static gboolean job_scan(RemminaFTPJob *job, RemminaFTPFs *fs, const gchar *src, const gchar *dst, guint top,
			 gint depth, gboolean post_order, gboolean follow_links, GPtrArray *plan)
{
	JobItem *item;
	GError *error = NULL;
	GPtrArray *children;
	gboolean descend;
	guint i;

	if (job_is_cancelled(job))
		return FALSE;

	item = g_new0(JobItem, 1);
	item->src = g_strdup(src);
	item->dst = g_strdup(dst);
	item->top = top;
	while (!remmina_ftp_fs_stat(fs, src, &item->entry, &error)) {
		if (!job_error(job, &error)) {
			job_item_free(item);
			return !job_is_cancelled(job);
		}
	}

	descend = item->entry.type == REMMINA_FTP_ENTRY_DIR && (follow_links || !item->entry.is_link) &&
		  (job->op != REMMINA_FTP_JOB_ATTRIBUTES || job->recursive);

	if (!post_order)
		g_ptr_array_add(plan, item);
	if (item->entry.type == REMMINA_FTP_ENTRY_FILE) {
		g_mutex_lock(&job->lock);
		job->bytes_total += item->entry.size;
		g_mutex_unlock(&job->lock);
	}

	if (descend && depth < JOB_MAX_DEPTH) {
		while (!(children = remmina_ftp_fs_list(fs, src, &error))) {
			if (!job_error(job, &error))
				break;
		}
		for (i = 0; children && i < children->len; i++) {
			RemminaFTPEntry *child = g_ptr_array_index(children, i);
			gchar *csrc = remmina_ftp_path_join(src, child->name);
			gchar *cdst = dst ? remmina_ftp_path_join(dst, child->name) : NULL;
			gboolean go = job_scan(job, fs, csrc, cdst, top, depth + 1, post_order, follow_links, plan);

			g_free(csrc);
			g_free(cdst);
			if (!go)
				break;
		}
		if (children)
			g_ptr_array_unref(children);
	}

	if (post_order)
		g_ptr_array_add(plan, item);

	g_mutex_lock(&job->lock);
	job->items_total = plan->len;
	g_mutex_unlock(&job->lock);
	job_progress(job, FALSE);
	return !job_is_cancelled(job);
}

/* ---- Copying ---- */

static RemminaFTPConflict job_conflict(RemminaFTPJob *job, JobItem *item, const gchar *dst, const RemminaFTPEntry *dst_entry,
				       gchar **new_name)
{
	RemminaFTPConflict answer = job->conflict_default;

	if (answer == REMMINA_FTP_CONFLICT_ASK) {
		answer = job->ui.ask_conflict ?
			 job->ui.ask_conflict(job, item->src, &item->entry, dst, dst_entry, new_name) :
			 REMMINA_FTP_CONFLICT_CANCEL;
		if (answer == REMMINA_FTP_CONFLICT_OVERWRITE_ALL || answer == REMMINA_FTP_CONFLICT_SKIP_ALL ||
		    answer == REMMINA_FTP_CONFLICT_OVERWRITE_OLDER_ALL || answer == REMMINA_FTP_CONFLICT_RESUME_ALL)
			job->conflict_default = answer;
	}

	switch (answer) {
	case REMMINA_FTP_CONFLICT_OVERWRITE_ALL:
		return REMMINA_FTP_CONFLICT_OVERWRITE;
	case REMMINA_FTP_CONFLICT_SKIP_ALL:
		return REMMINA_FTP_CONFLICT_SKIP;
	case REMMINA_FTP_CONFLICT_OVERWRITE_OLDER_ALL:
		return dst_entry->mtime < item->entry.mtime ? REMMINA_FTP_CONFLICT_OVERWRITE : REMMINA_FTP_CONFLICT_SKIP;
	case REMMINA_FTP_CONFLICT_RESUME_ALL:
		return REMMINA_FTP_CONFLICT_RESUME;
	default:
		return answer;
	}
}

static gboolean job_copy_data(RemminaFTPJob *job, RemminaFTPFs *sfs, RemminaFTPFs *dfs, JobItem *item, const gchar *dst,
			      guint64 offset, GError **error)
{
	RemminaFTPStream *in, *out;
	gchar *buf;
	gssize n;
	gboolean ok = FALSE;

	in = remmina_ftp_fs_open_read(sfs, item->src, offset, error);
	if (!in)
		return FALSE;
	out = remmina_ftp_fs_open_write(dfs, dst, offset > 0, error);
	if (!out) {
		remmina_ftp_stream_close(in);
		return FALSE;
	}

	job_add_bytes(job, offset);
	buf = g_malloc(JOB_BUFFER_SIZE);
	while (job_wait(job)) {
		n = remmina_ftp_stream_read(in, buf, JOB_BUFFER_SIZE, error);
		if (n < 0)
			break;
		if (n == 0) {
			ok = TRUE;
			break;
		}
		if (!remmina_ftp_stream_write(out, buf, (gsize)n, error))
			break;
		job_add_bytes(job, (guint64)n);
		job_progress(job, FALSE);
	}
	g_free(buf);
	remmina_ftp_stream_close(in);
	remmina_ftp_stream_close(out);
	return ok;
}

static void job_copy_item(RemminaFTPJob *job, RemminaFTPFs *sfs, RemminaFTPFs *dfs, JobItem *item)
{
	RemminaFTPEntry dst_entry = { 0 };
	GError *error = NULL;
	gchar *dst = g_strdup(item->dst);
	gboolean exists;
	guint64 offset;

	job_set_current(job, item->src, dst, item->entry.type == REMMINA_FTP_ENTRY_FILE ? item->entry.size : 0);

	if (item->entry.type == REMMINA_FTP_ENTRY_DIR) {
		for (;;) {
			exists = remmina_ftp_fs_stat(dfs, dst, &dst_entry, NULL);
			if (exists && dst_entry.type == REMMINA_FTP_ENTRY_DIR) {
				item->done = TRUE;
				break;
			}
			if (exists)
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_EXISTS, _("“%s” exists and is not a folder"), dst);
			else if (remmina_ftp_fs_mkdir(dfs, dst, &error)) {
				item->done = TRUE;
				break;
			}
			if (!job_error(job, &error))
				break;
		}
		g_free(dst_entry.name);
		g_free(dst_entry.owner);
		g_free(dst_entry.group);
		g_free(dst);
		return;
	}

	for (;;) {
		offset = 0;
		g_free(dst_entry.name);
		g_free(dst_entry.owner);
		g_free(dst_entry.group);
		memset(&dst_entry, 0, sizeof(dst_entry));
		exists = remmina_ftp_fs_stat(dfs, dst, &dst_entry, NULL);
		if (exists) {
			gchar *new_name = NULL;
			RemminaFTPConflict answer;

			if (dst_entry.type == REMMINA_FTP_ENTRY_DIR) {
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, _("“%s” is a folder"), dst);
				if (job_error(job, &error))
					continue;
				break;
			}
			answer = job_conflict(job, item, dst, &dst_entry, &new_name);
			if (answer == REMMINA_FTP_CONFLICT_CANCEL) {
				remmina_ftp_job_cancel(job);
				break;
			}
			if (answer == REMMINA_FTP_CONFLICT_SKIP) {
				g_mutex_lock(&job->lock);
				job->bytes_done += item->entry.size;
				g_mutex_unlock(&job->lock);
				break;
			}
			if (answer == REMMINA_FTP_CONFLICT_RENAME) {
				if (new_name && new_name[0]) {
					gchar *parent = remmina_ftp_path_parent(dst);
					g_free(dst);
					dst = remmina_ftp_path_join(parent, new_name);
					g_free(parent);
					job_set_current(job, item->src, dst, item->entry.size);
				}
				g_free(new_name);
				continue;	/* the new name may exist too */
			}
			if (answer == REMMINA_FTP_CONFLICT_RESUME) {
				if (dst_entry.size >= item->entry.size) {
					/* Nothing left to send */
					g_mutex_lock(&job->lock);
					job->bytes_done += item->entry.size;
					g_mutex_unlock(&job->lock);
					item->done = TRUE;
					break;
				}
				offset = dst_entry.size;
			}
		}

		job_rewind_file(job);
		if (job_copy_data(job, sfs, dfs, item, dst, offset, &error)) {
			/* Keep the date, and the rights for files that run */
			remmina_ftp_fs_set_mtime(dfs, dst, item->entry.mtime, NULL);
			remmina_ftp_fs_chmod(dfs, dst, item->entry.mode & 0777, NULL);
			item->done = TRUE;
			break;
		}
		if (job_is_cancelled(job)) {
			g_clear_error(&error);
			break;
		}
		if (!job_error(job, &error))
			break;
	}
	g_free(dst_entry.name);
	g_free(dst_entry.owner);
	g_free(dst_entry.group);
	g_free(dst);
}

/* Same side: a rename moves the whole tree at once */
static gboolean job_try_rename(RemminaFTPJob *job, RemminaFTPFs *fs, const gchar *src, const gchar *dst)
{
	RemminaFTPEntry entry = { 0 };
	gboolean exists = remmina_ftp_fs_stat(fs, dst, &entry, NULL);

	g_free(entry.name);
	g_free(entry.owner);
	g_free(entry.group);
	return !exists && remmina_ftp_fs_rename(fs, src, dst, NULL);
}

static gboolean job_into_itself(const gchar *src, const gchar *dst)
{
	gsize len = strlen(src);

	return g_strcmp0(src, dst) == 0 || (g_str_has_prefix(dst, src) && (dst[len] == '/' || src[len - 1] == '/'));
}

static void job_run_copy(RemminaFTPJob *job, RemminaFTPFs *sfs, RemminaFTPFs *dfs)
{
	GPtrArray *plan = g_ptr_array_new_with_free_func((GDestroyNotify)job_item_free);
	gboolean same_side = job->src_side == job->dst_side;
	guint i;

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_SCANNING;
	g_mutex_unlock(&job->lock);

	for (i = 0; i < job->sources->len && !job_is_cancelled(job); i++) {
		const gchar *src = g_ptr_array_index(job->sources, i);
		gchar *dst = remmina_ftp_path_join(job->dst_dir, remmina_ftp_path_basename(src));

		if (same_side && job_into_itself(src, dst)) {
			GError *error = NULL;
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				    _("“%s” cannot be copied into itself"), src);
			job_error(job, &error);
		} else if (!(job->op == REMMINA_FTP_JOB_MOVE && same_side && job_try_rename(job, sfs, src, dst))) {
			job_scan(job, sfs, src, dst, i, 0, FALSE, TRUE, plan);
		}
		g_free(dst);
	}

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_RUNNING;
	job->started_us = g_get_monotonic_time();
	g_mutex_unlock(&job->lock);
	job_progress(job, TRUE);

	for (i = 0; i < plan->len && job_wait(job); i++) {
		job_copy_item(job, sfs, dfs, g_ptr_array_index(plan, i));
		job_item_done(job);
	}

	/* Moving: remove what arrived, children first; a folder that still holds a skipped file stays */
	if (job->op == REMMINA_FTP_JOB_MOVE && !job_is_cancelled(job)) {
		for (i = plan->len; i > 0; i--) {
			JobItem *item = g_ptr_array_index(plan, i - 1);
			gboolean is_dir = item->entry.type == REMMINA_FTP_ENTRY_DIR && !item->entry.is_link;

			if (item->done)
				remmina_ftp_fs_remove(sfs, item->src, is_dir, NULL);
		}
	}
	g_ptr_array_unref(plan);
}

/* ---- Deleting and attributes ---- */

static void job_run_delete(RemminaFTPJob *job, RemminaFTPFs *fs)
{
	GPtrArray *plan = g_ptr_array_new_with_free_func((GDestroyNotify)job_item_free);
	guint i;

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_SCANNING;
	g_mutex_unlock(&job->lock);
	for (i = 0; i < job->sources->len && !job_is_cancelled(job); i++)
		job_scan(job, fs, g_ptr_array_index(job->sources, i), NULL, i, 0, TRUE, FALSE, plan);

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_RUNNING;
	job->started_us = g_get_monotonic_time();
	g_mutex_unlock(&job->lock);

	for (i = 0; i < plan->len && job_wait(job); i++) {
		JobItem *item = g_ptr_array_index(plan, i);
		gboolean is_dir = item->entry.type == REMMINA_FTP_ENTRY_DIR && !item->entry.is_link;
		GError *error = NULL;

		job_set_current(job, item->src, NULL, 0);
		while (!remmina_ftp_fs_remove(fs, item->src, is_dir, &error)) {
			if (!job_error(job, &error))
				break;
		}
		job_item_done(job);
	}
	g_ptr_array_unref(plan);
}

static void job_run_attributes(RemminaFTPJob *job, RemminaFTPFs *fs)
{
	GPtrArray *plan = g_ptr_array_new_with_free_func((GDestroyNotify)job_item_free);
	guint i;

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_SCANNING;
	g_mutex_unlock(&job->lock);
	for (i = 0; i < job->sources->len && !job_is_cancelled(job); i++)
		job_scan(job, fs, g_ptr_array_index(job->sources, i), NULL, i, 0, FALSE, FALSE, plan);

	g_mutex_lock(&job->lock);
	job->state = REMMINA_FTP_JOB_RUNNING;
	job->started_us = g_get_monotonic_time();
	g_mutex_unlock(&job->lock);

	for (i = 0; i < plan->len && job_wait(job); i++) {
		JobItem *item = g_ptr_array_index(plan, i);
		guint32 mode = (item->entry.mode & ~job->mode_clear) | job->mode_set;
		GError *error = NULL;

		job_set_current(job, item->src, NULL, 0);
		/* chmod on a link would change what it points to */
		if (!item->entry.is_link) {
			while (mode != item->entry.mode && !remmina_ftp_fs_chmod(fs, item->src, mode, &error)) {
				if (!job_error(job, &error))
					break;
			}
			while (job->set_mtime && !remmina_ftp_fs_set_mtime(fs, item->src, job->mtime, &error)) {
				if (!job_error(job, &error))
					break;
			}
		}
		job_item_done(job);
	}
	g_ptr_array_unref(plan);
}

void remmina_ftp_job_run(RemminaFTPJob *job, RemminaFTPFs *local, RemminaFTPFs *remote)
{
	RemminaFTPFs *sfs = job->src_side == REMMINA_FTP_SIDE_LOCAL ? local : remote;
	RemminaFTPFs *dfs = job->dst_side == REMMINA_FTP_SIDE_LOCAL ? local : remote;

	if (sfs && !job_is_cancelled(job)) {
		switch (job->op) {
		case REMMINA_FTP_JOB_COPY:
		case REMMINA_FTP_JOB_MOVE:
			if (dfs)
				job_run_copy(job, sfs, dfs);
			break;
		case REMMINA_FTP_JOB_DELETE:
			job_run_delete(job, sfs);
			break;
		case REMMINA_FTP_JOB_ATTRIBUTES:
			job_run_attributes(job, sfs);
			break;
		}
	}

	g_mutex_lock(&job->lock);
	job->state = job->cancelled ? REMMINA_FTP_JOB_CANCELLED : REMMINA_FTP_JOB_DONE;
	g_mutex_unlock(&job->lock);
	if (job->ui.finished)
		job->ui.finished(job);
}
