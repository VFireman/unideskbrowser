/*
 * Unidesk - single connection database
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Profile identifiers look like "db:0123456789abcdef" and are used where Remmina used file names */
#define REMMINA_DB_ID_PREFIX "db:"

/* Load (or create) the database file; safe to call several times */
gboolean remmina_db_init(void);
/* The database file path */
const gchar *remmina_db_get_path(void);
/* TRUE when the string is a database identifier rather than a file name */
gboolean remmina_db_is_id(const gchar *name);
/* A fresh unique identifier (caller frees) */
gchar *remmina_db_new_id(void);
gboolean remmina_db_profile_exists(const gchar *id);
/* NULL terminated list of profile identifiers (caller frees with g_strfreev) */
gchar **remmina_db_list_profiles(gsize *count);
/* Settings of a profile as a string->string hash table (caller destroys), NULL if missing */
GHashTable *remmina_db_profile_load(const gchar *id);
/* Replace the settings of a profile (creates it) and write the database */
gboolean remmina_db_profile_save(const gchar *id, GHashTable *settings);
/* Delete a profile and its state */
gboolean remmina_db_profile_delete(const gchar *id);
/* Runtime state of a profile (last success date, geometry…), separate from its settings */
gchar *remmina_db_state_get(const gchar *id, const gchar *key);
void remmina_db_state_set(const gchar *id, const gchar *key, const gchar *value);
/* Last use time (unix time), 0 when never used */
gint64 remmina_db_profile_get_last_used(const gchar *id);
void remmina_db_profile_set_last_used(const gchar *id, gint64 unix_time);
void remmina_db_profile_touch(const gchar *id);
/* How many times the connection was made ("use_count" in its state), for the frequently used ones
 * in the history. A connection made before the count existed counts once; 0: never made, or taken
 * off the frequently used */
gint remmina_db_use_count_get(const gchar *id);
void remmina_db_use_count_set(const gchar *id, gint count);
/* Several at once, written once: profile id → GINT_TO_POINTER(count) */
void remmina_db_use_counts_set(GHashTable *counts);
/* Write the database to disk (atomic replace) */
gboolean remmina_db_save(void);
/* Free-form metadata in the [unidesk] group (migration markers…) */
gchar *remmina_db_meta_get(const gchar *key);
void remmina_db_meta_set(const gchar *key, const gchar *value);

G_END_DECLS
