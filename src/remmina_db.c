/*
 * Unidesk - single connection database
 *
 * Every connection profile is a group of one GKeyFile stored in
 * <config dir>/connections.db instead of a separate .remmina file:
 *
 *   [unidesk]            version=1
 *   [profile:<id>]       the settings (same keys as a .remmina file; secrets are
 *                        kept in the vault, the setting only holds the "." marker)
 *   [state:<id>]         runtime state (last_success, use_count…)
 *
 * The identifier "db:<id>" plays the role of the old file name everywhere in
 * the code (RemminaFile->filename), so the rest of the application and the
 * plugins keep working unchanged.
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
#include <glib/gstdio.h>
#include <string.h>

#include "remmina_db.h"
#include "remmina_log.h"
#include "remmina/remmina_trace_calls.h"

#define DB_FILE_NAME		"connections.db"
#define DB_CONFIG_SUBDIR	"unidesk"
#define DB_GROUP_META		"unidesk"
#define DB_PROFILE_PREFIX	"profile:"
#define DB_STATE_PREFIX		"state:"
#define DB_KEY_LAST_USED	"last_used"
#define DB_KEY_USE_COUNT	"use_count"

static GKeyFile *db = NULL;
static gchar *db_path = NULL;

const gchar *remmina_db_get_path(void)
{
	if (!db_path)
		db_path = g_build_filename(g_get_user_config_dir(), DB_CONFIG_SUBDIR, DB_FILE_NAME, NULL);
	return db_path;
}

gboolean remmina_db_init(void)
{
	TRACE_CALL(__func__);
	GError *err = NULL;

	if (db)
		return TRUE;
	db = g_key_file_new();
	if (g_file_test(remmina_db_get_path(), G_FILE_TEST_IS_REGULAR)) {
		if (!g_key_file_load_from_file(db, remmina_db_get_path(), G_KEY_FILE_KEEP_COMMENTS, &err)) {
			gchar *broken = g_strconcat(remmina_db_get_path(), ".broken", NULL);
			REMMINA_WARNING("The connection database %s cannot be read (%s): it is moved to %s and recreated",
					remmina_db_get_path(), err ? err->message : "", broken);
			g_clear_error(&err);
			g_rename(remmina_db_get_path(), broken);
			g_free(broken);
			g_key_file_free(db);
			db = g_key_file_new();
		}
	}
	if (!g_key_file_has_group(db, DB_GROUP_META)) {
		g_key_file_set_integer(db, DB_GROUP_META, "version", 1);
		remmina_db_save();
	}
	return TRUE;
}

gboolean remmina_db_save(void)
{
	TRACE_CALL(__func__);
	gchar *data, *dir, *tmp;
	gsize len = 0;
	GError *err = NULL;
	gboolean ok;

	if (!db)
		return FALSE;
	data = g_key_file_to_data(db, &len, NULL);
	if (!data)
		return FALSE;
	dir = g_path_get_dirname(remmina_db_get_path());
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	tmp = g_strconcat(remmina_db_get_path(), ".tmp", NULL);
	ok = g_file_set_contents(tmp, data, len, &err);
	if (ok) {
		g_chmod(tmp, 0600);
		if (g_rename(tmp, remmina_db_get_path()) != 0) {
			REMMINA_WARNING("Cannot replace the connection database %s", remmina_db_get_path());
			ok = FALSE;
		}
	} else {
		REMMINA_WARNING("Cannot write the connection database: %s", err ? err->message : "");
		g_clear_error(&err);
	}
	g_free(tmp);
	g_free(data);
	return ok;
}

gchar *remmina_db_meta_get(const gchar *key)
{
	if (!db || !key)
		return NULL;
	return g_key_file_get_string(db, DB_GROUP_META, key, NULL);
}

void remmina_db_meta_set(const gchar *key, const gchar *value)
{
	if (!db || !key)
		return;
	if (value)
		g_key_file_set_string(db, DB_GROUP_META, key, value);
	else
		g_key_file_remove_key(db, DB_GROUP_META, key, NULL);
	remmina_db_save();
}

gboolean remmina_db_is_id(const gchar *name)
{
	return name != NULL && g_str_has_prefix(name, REMMINA_DB_ID_PREFIX);
}

static gchar *remmina_db_group_for(const gchar *prefix, const gchar *id)
{
	const gchar *bare = remmina_db_is_id(id) ? id + strlen(REMMINA_DB_ID_PREFIX) : id;

	return g_strconcat(prefix, bare, NULL);
}

gchar *remmina_db_new_id(void)
{
	gchar *id;

	do {
		guint32 a = g_random_int(), b = g_random_int();
		id = g_strdup_printf(REMMINA_DB_ID_PREFIX "%08x%08x", a, b);
	} while (remmina_db_profile_exists(id));
	return id;
}

gboolean remmina_db_profile_exists(const gchar *id)
{
	gchar *group;
	gboolean r;

	if (!db || !id)
		return FALSE;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	r = g_key_file_has_group(db, group);
	g_free(group);
	return r;
}

gchar **remmina_db_list_profiles(gsize *count)
{
	gchar **groups, **ids;
	gsize n = 0, i, k = 0;

	if (count)
		*count = 0;
	if (!db)
		return g_new0(gchar *, 1);
	groups = g_key_file_get_groups(db, &n);
	ids = g_new0(gchar *, n + 1);
	for (i = 0; i < n; i++) {
		if (g_str_has_prefix(groups[i], DB_PROFILE_PREFIX))
			ids[k++] = g_strconcat(REMMINA_DB_ID_PREFIX, groups[i] + strlen(DB_PROFILE_PREFIX), NULL);
	}
	g_strfreev(groups);
	if (count)
		*count = k;
	return ids;
}

GHashTable *remmina_db_profile_load(const gchar *id)
{
	TRACE_CALL(__func__);
	GHashTable *settings;
	gchar *group, **keys;
	gsize n = 0, i;

	if (!db || !id)
		return NULL;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	if (!g_key_file_has_group(db, group)) {
		g_free(group);
		return NULL;
	}
	settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	keys = g_key_file_get_keys(db, group, &n, NULL);
	for (i = 0; i < n; i++) {
		if (g_strcmp0(keys[i], DB_KEY_LAST_USED) == 0)
			continue;
		g_hash_table_insert(settings, g_strdup(keys[i]), g_key_file_get_string(db, group, keys[i], NULL));
	}
	g_strfreev(keys);
	g_free(group);
	return settings;
}

gboolean remmina_db_profile_save(const gchar *id, GHashTable *settings)
{
	TRACE_CALL(__func__);
	gchar *group, *last_used = NULL;
	GHashTableIter iter;
	gpointer key, value;

	if (!db || !id || !settings)
		return FALSE;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	if (g_key_file_has_group(db, group)) {
		last_used = g_key_file_get_string(db, group, DB_KEY_LAST_USED, NULL);
		g_key_file_remove_group(db, group, NULL);
	}
	g_hash_table_iter_init(&iter, settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (value)
			g_key_file_set_string(db, group, (const gchar *)key, (const gchar *)value);
	}
	if (last_used) {
		g_key_file_set_string(db, group, DB_KEY_LAST_USED, last_used);
		g_free(last_used);
	}
	g_free(group);
	return remmina_db_save();
}

gboolean remmina_db_profile_delete(const gchar *id)
{
	TRACE_CALL(__func__);
	gchar *group;
	gboolean r;

	if (!db || !id)
		return FALSE;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	r = g_key_file_remove_group(db, group, NULL);
	g_free(group);
	group = remmina_db_group_for(DB_STATE_PREFIX, id);
	g_key_file_remove_group(db, group, NULL);
	g_free(group);
	remmina_db_save();
	return r;
}

gchar *remmina_db_state_get(const gchar *id, const gchar *key)
{
	gchar *group, *v;

	if (!db || !id || !key)
		return NULL;
	group = remmina_db_group_for(DB_STATE_PREFIX, id);
	v = g_key_file_get_string(db, group, key, NULL);
	g_free(group);
	return v;
}

void remmina_db_state_set(const gchar *id, const gchar *key, const gchar *value)
{
	gchar *group;

	if (!db || !id || !key)
		return;
	group = remmina_db_group_for(DB_STATE_PREFIX, id);
	if (value)
		g_key_file_set_string(db, group, key, value);
	else
		g_key_file_remove_key(db, group, key, NULL);
	g_free(group);
	remmina_db_save();
}

gint64 remmina_db_profile_get_last_used(const gchar *id)
{
	gchar *group;
	gint64 v;

	if (!db || !id)
		return 0;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	v = g_key_file_get_int64(db, group, DB_KEY_LAST_USED, NULL);
	g_free(group);
	return v;
}

void remmina_db_profile_set_last_used(const gchar *id, gint64 unix_time)
{
	gchar *group;

	if (!db || !id)
		return;
	group = remmina_db_group_for(DB_PROFILE_PREFIX, id);
	if (g_key_file_has_group(db, group)) {
		g_key_file_set_int64(db, group, DB_KEY_LAST_USED, unix_time);
		remmina_db_save();
	}
	g_free(group);
}

void remmina_db_profile_touch(const gchar *id)
{
	remmina_db_profile_set_last_used(id, g_get_real_time() / G_USEC_PER_SEC);
}

gint remmina_db_use_count_get(const gchar *id)
{
	gchar *group;
	gint v = 0;

	if (!db || !id)
		return 0;
	group = remmina_db_group_for(DB_STATE_PREFIX, id);
	if (g_key_file_has_key(db, group, DB_KEY_USE_COUNT, NULL))
		v = MAX(0, g_key_file_get_integer(db, group, DB_KEY_USE_COUNT, NULL));
	else if (g_key_file_has_key(db, group, "last_success", NULL))
		v = 1;
	g_free(group);
	return v;
}

static gboolean remmina_db_use_count_put(const gchar *id, gint count)
{
	gchar *group;

	if (!id || !remmina_db_profile_exists(id))
		return FALSE;
	group = remmina_db_group_for(DB_STATE_PREFIX, id);
	g_key_file_set_integer(db, group, DB_KEY_USE_COUNT, MAX(0, count));
	g_free(group);
	return TRUE;
}

void remmina_db_use_count_set(const gchar *id, gint count)
{
	if (db && remmina_db_use_count_put(id, count))
		remmina_db_save();
}

void remmina_db_use_counts_set(GHashTable *counts)
{
	GHashTableIter iter;
	gpointer id, count;
	gboolean changed = FALSE;

	if (!db || !counts)
		return;
	g_hash_table_iter_init(&iter, counts);
	while (g_hash_table_iter_next(&iter, &id, &count))
		changed |= remmina_db_use_count_put(id, GPOINTER_TO_INT(count));
	if (changed)
		remmina_db_save();
}
