/*
 * Unidesk - sessions: the tabs of a window, kept when it closes and at quit
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
 * sessions.db:
 *   [unidesk-sessions]   version=1, next_number=<n>, next_color=<n>
 *   [session:<id>]       name, saved, state (open|closed), updated_at, closed_at,
 *                        x, y, width, height, maximized, active, tabs=<count>,
 *                        tab<i>.profile / .name / .protocol / .server / .username / .sftp_left / .sftp_right
 */

#include "config.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>

#include "remmina_session.h"
#include "remmina_log.h"
#include "remmina/remmina_trace_calls.h"

#define SESSIONS_FILE_NAME	"sessions.db"
#define SESSIONS_SUBDIR		"unidesk"
#define SESSIONS_GROUP_META	"unidesk-sessions"
#define SESSIONS_PREFIX		"session:"
#define SESSIONS_VERSION	1

static GKeyFile *sessions = NULL;
static gchar *sessions_path = NULL;

static const gchar *session_path(void)
{
	if (!sessions_path)
		sessions_path = g_build_filename(g_get_user_config_dir(), SESSIONS_SUBDIR, SESSIONS_FILE_NAME, NULL);
	return sessions_path;
}

static void session_save(void)
{
	gchar *data, *dir, *tmp;
	GError *err = NULL;
	gsize len = 0;

	data = g_key_file_to_data(sessions, &len, NULL);
	if (!data)
		return;
	dir = g_path_get_dirname(session_path());
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	/* Atomically: a crash leaves the old file or the new one, never half of one */
	tmp = g_strconcat(session_path(), ".tmp", NULL);
	if (g_file_set_contents(tmp, data, len, &err)) {
		g_chmod(tmp, 0600);
		if (g_rename(tmp, session_path()) != 0)
			REMMINA_WARNING("Cannot replace the sessions file %s", session_path());
	} else {
		REMMINA_WARNING("Cannot write the sessions file: %s", err ? err->message : "");
		g_clear_error(&err);
	}
	g_free(tmp);
	g_free(data);
}

static GKeyFile *session_file(void)
{
	GError *err = NULL;

	if (sessions)
		return sessions;
	sessions = g_key_file_new();
	if (g_file_test(session_path(), G_FILE_TEST_IS_REGULAR) &&
	    !g_key_file_load_from_file(sessions, session_path(), G_KEY_FILE_NONE, &err)) {
		/* Never lose it silently: keep the unreadable file aside */
		gchar *broken = g_strconcat(session_path(), ".broken", NULL);
		REMMINA_WARNING("The sessions file %s cannot be read (%s): it is moved to %s", session_path(),
				err ? err->message : "", broken);
		g_clear_error(&err);
		g_rename(session_path(), broken);
		g_free(broken);
		g_key_file_free(sessions);
		sessions = g_key_file_new();
	}
	if (g_key_file_get_integer(sessions, SESSIONS_GROUP_META, "version", NULL) > SESSIONS_VERSION)
		REMMINA_WARNING("The sessions file %s comes from a newer version", session_path());
	g_key_file_set_integer(sessions, SESSIONS_GROUP_META, "version", SESSIONS_VERSION);
	return sessions;
}

static gchar *session_group(const gchar *id)
{
	return g_strconcat(SESSIONS_PREFIX, id, NULL);
}

/* ---- Objects ---- */

RemminaSessionTab *remmina_session_tab_new(void)
{
	return g_new0(RemminaSessionTab, 1);
}

void remmina_session_tab_free(RemminaSessionTab *tab)
{
	if (!tab)
		return;
	g_free(tab->profile);
	g_free(tab->name);
	g_free(tab->protocol);
	g_free(tab->server);
	g_free(tab->username);
	g_free(tab->sftp_left);
	g_free(tab->sftp_right);
	g_free(tab);
}

static RemminaSession *session_alloc(void)
{
	RemminaSession *s = g_new0(RemminaSession, 1);

	s->tabs = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_tab_free);
	s->active = -1;
	return s;
}

RemminaSession *remmina_session_new(void)
{
	GKeyFile *kf = session_file();
	RemminaSession *s = session_alloc();
	gint number;

	do {
		gchar *group;
		g_free(s->id);
		s->id = g_strdup_printf("s%08x%08x", g_random_int(), g_random_int());
		group = session_group(s->id);
		if (!g_key_file_has_group(kf, group)) {
			g_free(group);
			break;
		}
		g_free(group);
	} while (TRUE);
	number = MAX(1, g_key_file_get_integer(kf, SESSIONS_GROUP_META, "next_number", NULL));
	g_key_file_set_integer(kf, SESSIONS_GROUP_META, "next_number", number + 1);
	/* TRANSLATORS: the name of a new session (the tabs of a window) until the user names it */
	s->name = g_strdup_printf(_("Session %d"), number);
	s->state = REMMINA_SESSION_OPEN;
	s->updated_at = g_get_real_time();
	return s;
}

gint remmina_session_next_color(void)
{
	GKeyFile *kf = session_file();
	gint n = MAX(0, g_key_file_get_integer(kf, SESSIONS_GROUP_META, "next_color", NULL));

	g_key_file_set_integer(kf, SESSIONS_GROUP_META, "next_color", n + 1);
	session_save();
	return n;
}

RemminaSession *remmina_session_copy(const RemminaSession *session)
{
	RemminaSession *s;
	guint i;

	if (!session)
		return NULL;
	s = session_alloc();
	s->id = g_strdup(session->id);
	s->name = g_strdup(session->name);
	s->saved = session->saved;
	s->state = session->state;
	s->updated_at = session->updated_at;
	s->closed_at = session->closed_at;
	s->x = session->x;
	s->y = session->y;
	s->width = session->width;
	s->height = session->height;
	s->maximized = session->maximized;
	s->active = session->active;
	s->color = g_strdup(session->color);
	for (i = 0; i < session->tabs->len; i++) {
		const RemminaSessionTab *t = g_ptr_array_index(session->tabs, i);
		RemminaSessionTab *c = remmina_session_tab_new();
		c->profile = g_strdup(t->profile);
		c->name = g_strdup(t->name);
		c->protocol = g_strdup(t->protocol);
		c->server = g_strdup(t->server);
		c->username = g_strdup(t->username);
		c->sftp_left = g_strdup(t->sftp_left);
		c->sftp_right = g_strdup(t->sftp_right);
		g_ptr_array_add(s->tabs, c);
	}
	return s;
}

void remmina_session_free(RemminaSession *session)
{
	if (!session)
		return;
	g_free(session->id);
	g_free(session->name);
	g_free(session->color);
	g_ptr_array_unref(session->tabs);
	g_free(session);
}

/* ---- The file ---- */

static void set_opt_string(GKeyFile *kf, const gchar *group, const gchar *key, const gchar *value)
{
	if (value && value[0])
		g_key_file_set_string(kf, group, key, value);
}

static void session_write(GKeyFile *kf, const RemminaSession *s)
{
	gchar *group = session_group(s->id);
	guint i;

	g_key_file_remove_group(kf, group, NULL);
	g_key_file_set_string(kf, group, "name", s->name ? s->name : "");
	g_key_file_set_boolean(kf, group, "saved", s->saved);
	g_key_file_set_string(kf, group, "state", s->state == REMMINA_SESSION_OPEN ? "open" : "closed");
	g_key_file_set_int64(kf, group, "updated_at", s->updated_at);
	g_key_file_set_int64(kf, group, "closed_at", s->closed_at);
	g_key_file_set_integer(kf, group, "x", s->x);
	g_key_file_set_integer(kf, group, "y", s->y);
	g_key_file_set_integer(kf, group, "width", s->width);
	g_key_file_set_integer(kf, group, "height", s->height);
	g_key_file_set_boolean(kf, group, "maximized", s->maximized);
	g_key_file_set_integer(kf, group, "active", s->active);
	set_opt_string(kf, group, "color", s->color);
	g_key_file_set_integer(kf, group, "tabs", (gint)s->tabs->len);
	for (i = 0; i < s->tabs->len; i++) {
		const RemminaSessionTab *t = g_ptr_array_index(s->tabs, i);
		gchar *key;
#define TAB_KEY(field) (key = g_strdup_printf("tab%u." #field, i))
		set_opt_string(kf, group, TAB_KEY(profile), t->profile);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(name), t->name);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(protocol), t->protocol);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(server), t->server);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(username), t->username);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(sftp_left), t->sftp_left);
		g_free(key);
		set_opt_string(kf, group, TAB_KEY(sftp_right), t->sftp_right);
		g_free(key);
#undef TAB_KEY
	}
	g_free(group);
}

static gchar *get_opt_string(GKeyFile *kf, const gchar *group, const gchar *key)
{
	gchar *v = g_key_file_get_string(kf, group, key, NULL);

	if (v && !v[0]) {
		g_free(v);
		v = NULL;
	}
	return v;
}

static RemminaSession *session_read(GKeyFile *kf, const gchar *group)
{
	RemminaSession *s = session_alloc();
	gchar *state;
	gint i, n;

	s->id = g_strdup(group + strlen(SESSIONS_PREFIX));
	s->name = g_key_file_get_string(kf, group, "name", NULL);
	s->saved = g_key_file_get_boolean(kf, group, "saved", NULL);
	state = g_key_file_get_string(kf, group, "state", NULL);
	s->state = g_strcmp0(state, "open") == 0 ? REMMINA_SESSION_OPEN : REMMINA_SESSION_CLOSED;
	g_free(state);
	s->updated_at = g_key_file_get_int64(kf, group, "updated_at", NULL);
	s->closed_at = g_key_file_get_int64(kf, group, "closed_at", NULL);
	s->x = g_key_file_get_integer(kf, group, "x", NULL);
	s->y = g_key_file_get_integer(kf, group, "y", NULL);
	s->width = g_key_file_get_integer(kf, group, "width", NULL);
	s->height = g_key_file_get_integer(kf, group, "height", NULL);
	s->maximized = g_key_file_get_boolean(kf, group, "maximized", NULL);
	s->active = g_key_file_has_key(kf, group, "active", NULL) ? g_key_file_get_integer(kf, group, "active", NULL) : -1;
	s->color = get_opt_string(kf, group, "color");
	n = CLAMP(g_key_file_get_integer(kf, group, "tabs", NULL), 0, 1000);
	for (i = 0; i < n; i++) {
		RemminaSessionTab *t = remmina_session_tab_new();
		gchar *key;
#define TAB_GET(field) (key = g_strdup_printf("tab%d." #field, i), t->field = get_opt_string(kf, group, key), g_free(key))
		TAB_GET(profile);
		TAB_GET(name);
		TAB_GET(protocol);
		TAB_GET(server);
		TAB_GET(username);
		TAB_GET(sftp_left);
		TAB_GET(sftp_right);
#undef TAB_GET
		/* A tab nothing can be opened from is dropped */
		if (t->profile || (t->protocol && t->server))
			g_ptr_array_add(s->tabs, t);
		else
			remmina_session_tab_free(t);
	}
	if (s->active >= (gint)s->tabs->len)
		s->active = (gint)s->tabs->len - 1;
	return s;
}

/* Keep the named closed sessions and the newest unnamed ones */
static void session_prune(GKeyFile *kf)
{
	GPtrArray *closed = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_free);
	gchar **groups = g_key_file_get_groups(kf, NULL);
	guint i;

	for (i = 0; groups[i]; i++) {
		RemminaSession *s;
		if (!g_str_has_prefix(groups[i], SESSIONS_PREFIX))
			continue;
		s = session_read(kf, groups[i]);
		if (s->state == REMMINA_SESSION_CLOSED && !s->saved)
			g_ptr_array_add(closed, s);
		else
			remmina_session_free(s);
	}
	g_strfreev(groups);
	if (closed->len > REMMINA_SESSION_KEEP_CLOSED) {
		/* Oldest first, drop until the newest REMMINA_SESSION_KEEP_CLOSED remain */
		guint drop = closed->len - REMMINA_SESSION_KEEP_CLOSED;
		while (drop-- > 0) {
			guint oldest = 0, j;
			for (j = 1; j < closed->len; j++)
				if (((RemminaSession *)g_ptr_array_index(closed, j))->closed_at <
				    ((RemminaSession *)g_ptr_array_index(closed, oldest))->closed_at)
					oldest = j;
			gchar *group = session_group(((RemminaSession *)g_ptr_array_index(closed, oldest))->id);
			g_key_file_remove_group(kf, group, NULL);
			g_free(group);
			g_ptr_array_remove_index_fast(closed, oldest);
		}
	}
	g_ptr_array_unref(closed);
}

void remmina_session_store(const RemminaSession *session)
{
	GKeyFile *kf = session_file();

	if (!session || !session->id)
		return;
	session_write(kf, session);
	session_save();
}

RemminaSession *remmina_session_lookup(const gchar *id)
{
	GKeyFile *kf = session_file();
	gchar *group;
	RemminaSession *s = NULL;

	if (!id)
		return NULL;
	group = session_group(id);
	if (g_key_file_has_group(kf, group))
		s = session_read(kf, group);
	g_free(group);
	return s;
}

static gint session_cmp(gconstpointer a, gconstpointer b)
{
	const RemminaSession *sa = *(RemminaSession *const *)a;
	const RemminaSession *sb = *(RemminaSession *const *)b;
	gint64 ta = sa->state == REMMINA_SESSION_CLOSED ? sa->closed_at : sa->updated_at;
	gint64 tb = sb->state == REMMINA_SESSION_CLOSED ? sb->closed_at : sb->updated_at;

	if (sa->state != sb->state)
		return sa->state == REMMINA_SESSION_OPEN ? -1 : 1;
	return ta > tb ? -1 : (ta < tb ? 1 : 0);
}

GPtrArray *remmina_session_list(void)
{
	GKeyFile *kf = session_file();
	GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_free);
	gchar **groups = g_key_file_get_groups(kf, NULL);
	guint i;

	for (i = 0; groups[i]; i++)
		if (g_str_has_prefix(groups[i], SESSIONS_PREFIX))
			g_ptr_array_add(list, session_read(kf, groups[i]));
	g_strfreev(groups);
	g_ptr_array_sort(list, session_cmp);
	return list;
}

void remmina_session_delete(const gchar *id)
{
	GKeyFile *kf = session_file();
	gchar *group;

	if (!id)
		return;
	group = session_group(id);
	if (g_key_file_remove_group(kf, group, NULL))
		session_save();
	g_free(group);
}

void remmina_session_rename(const gchar *id, const gchar *name)
{
	RemminaSession *s = remmina_session_lookup(id);

	if (!s || !name || !name[0]) {
		remmina_session_free(s);
		return;
	}
	g_free(s->name);
	s->name = g_strdup(name);
	s->saved = TRUE;
	remmina_session_store(s);
	remmina_session_free(s);
}

void remmina_session_set_saved(const gchar *id, gboolean saved)
{
	RemminaSession *s = remmina_session_lookup(id);

	if (!s)
		return;
	s->saved = saved;
	remmina_session_store(s);
	remmina_session_free(s);
}

void remmina_session_close(const gchar *id)
{
	GKeyFile *kf = session_file();
	RemminaSession *s = remmina_session_lookup(id);

	if (!s)
		return;
	if (s->tabs->len == 0 && !s->saved) {
		/* Nothing to bring back */
		remmina_session_delete(id);
		remmina_session_free(s);
		return;
	}
	s->state = REMMINA_SESSION_CLOSED;
	s->closed_at = g_get_real_time();
	session_write(kf, s);
	session_prune(kf);
	session_save();
	remmina_session_free(s);
}

GPtrArray *remmina_session_take_continue_set(void)
{
	GKeyFile *kf = session_file();
	GPtrArray *all = remmina_session_list();
	GPtrArray *open = g_ptr_array_new_with_free_func((GDestroyNotify)remmina_session_free);
	gboolean changed = FALSE;
	guint i;

	for (i = 0; i < all->len; i++) {
		RemminaSession *s = g_ptr_array_index(all, i);
		if (s->state != REMMINA_SESSION_OPEN)
			continue;
		if (s->tabs->len == 0 && !s->saved) {
			/* An empty window: nothing to continue */
			gchar *group = session_group(s->id);
			g_key_file_remove_group(kf, group, NULL);
			g_free(group);
			changed = TRUE;
			continue;
		}
		g_ptr_array_add(open, remmina_session_copy(s));
		/* Among the recently closed until "Continue" opens it again */
		s->state = REMMINA_SESSION_CLOSED;
		s->closed_at = s->updated_at ? s->updated_at : g_get_real_time();
		session_write(kf, s);
		changed = TRUE;
	}
	g_ptr_array_unref(all);
	if (changed) {
		session_prune(kf);
		session_save();
	}
	/* The window used last first: it reopens in the window already there */
	g_ptr_array_sort(open, session_cmp);
	return open;
}
