/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009-2010 Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
 * Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
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

#include "config.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#include "remmina_log.h"
#include "remmina_public.h"
#include "remmina_pref.h"
#include "remmina_string_array.h"
#include "remmina_plugin_manager.h"
#include "remmina_file_manager.h"
#include "remmina_file.h"
#include "remmina_db.h"
#include "remmina_vault.h"
#include "remmina_crypt.h"
#include "remmina/remmina_trace_calls.h"

static gchar *remminadir;
static gchar *cachedir;

/**
 * Return datadir_path from pref or first found data dir as per XDG specs.
 *
 * The returned string must be freed by the caller with g_free
 */
gchar *remmina_file_get_datadir(void)
{
	TRACE_CALL(__func__);
	const gchar *dir = ".remmina";
	int i;

	/* From preferences, datadir_path */
	remminadir = remmina_pref_get_value("datadir_path");
	if (remminadir != NULL && strlen(remminadir) > 0)
		if (g_file_test(remminadir, G_FILE_TEST_IS_DIR))
			return remminadir;
	g_free(remminadir), remminadir = NULL;
	/* Legacy ~/.remmina */
	remminadir = g_build_path("/", g_get_home_dir(), dir, NULL);
	if (g_file_test(remminadir, G_FILE_TEST_IS_DIR))
		return remminadir;
	g_free(remminadir), remminadir = NULL;
	/* ~/.local/share/unidesk */
	remminadir = g_build_path("/", g_get_user_data_dir(), "unidesk", NULL);
	if (g_file_test(remminadir, G_FILE_TEST_IS_DIR))
		return remminadir;
	g_free(remminadir), remminadir = NULL;
	/* /usr/local/share/unidesk */
	const gchar *const *dirs = g_get_system_data_dirs();

	g_free(remminadir), remminadir = NULL;
	for (i = 0; dirs[i] != NULL; ++i) {
		remminadir = g_build_path("/", dirs[i], "unidesk", NULL);
		if (g_file_test(remminadir, G_FILE_TEST_IS_DIR))
			return remminadir;
		g_free(remminadir), remminadir = NULL;
	}
	/* The last case we use  the home ~/.local/share/unidesk */
	remminadir = g_build_path("/", g_get_user_data_dir(), "unidesk", NULL);
	return remminadir;
}

/** @todo remmina_pref_file_do_copy and remmina_file_manager_do_copy to remmina_files_copy */
static gboolean remmina_file_manager_do_copy(const char *src_path, const char *dst_path)
{
	GFile *src = g_file_new_for_path(src_path), *dst = g_file_new_for_path(dst_path);
	/* We don’t overwrite the target if it exists */
	const gboolean ok = g_file_copy(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);

	g_object_unref(dst);
	g_object_unref(src);

	return ok;
}

/**
 * It creates the Remmina data and cache folders
 *
 * If it finds the legacy ~/.remmina folder it copies the connection profiles into the new folder.
 *
 * If it finds default profiles in the XDG_DATA_DIRS it copies the profiles into the user data-folder.
 */
void remmina_file_manager_init(void)
{
	TRACE_CALL(__func__);
	GDir *dir;
	const gchar *legacy = ".remmina";
	const gchar *filename;
	int i;

	/* Get and create the XDG_DATA_HOME directory */
	remminadir = remmina_pref_get_value("datadir_path");
	if (g_mkdir_with_parents(remminadir, 0750) == 0) {
		REMMINA_DEBUG("Initialized the \"%s\" data folder", remminadir);
		g_free(remminadir), remminadir = NULL;
	} else {
		g_free(remminadir), remminadir = NULL;
		/* Get and create the XDG_DATA_HOME directory */
		remminadir = g_build_path("/", g_get_user_data_dir(), "unidesk", NULL);
		if (g_mkdir_with_parents(remminadir, 0750) == 0)
			REMMINA_DEBUG("Initialized the \"%s\" data folder", remminadir);
		else
			REMMINA_CRITICAL("Cannot create the \"%s\" data folder", remminadir);
		g_free(remminadir), remminadir = NULL;
	}
	/* Create the XDG_CACHE_HOME directory */
	cachedir = g_build_path("/", g_get_user_cache_dir(), "unidesk", NULL);
	g_mkdir_with_parents(cachedir, 0750);
	g_free(cachedir), cachedir = NULL;
	/* Empty legacy ~/.remmina */
	remminadir = g_build_path("/", g_get_home_dir(), legacy, NULL);
	if (g_file_test(remminadir, G_FILE_TEST_IS_DIR)) {
		dir = g_dir_open(remminadir, 0, NULL);
		while ((filename = g_dir_read_name(dir)) != NULL) {
			remmina_file_manager_do_copy(
				g_build_path("/", remminadir, filename, NULL),
				g_build_path("/", g_get_user_data_dir(),
					     "unidesk", filename, NULL));
		}
		g_dir_close(dir);
	}

	/* XDG_DATA_DIRS, i.e. /usr/local/share/unidesk */
	const gchar *const *dirs = g_get_system_data_dirs();

	g_free(remminadir), remminadir = NULL;
	for (i = 0; dirs[i] != NULL; ++i) {
		remminadir = g_build_path("/", dirs[i], "unidesk", NULL);
		if (g_file_test(remminadir, G_FILE_TEST_IS_DIR)) {
			dir = g_dir_open(remminadir, 0, NULL);
			while ((filename = g_dir_read_name(dir)) != NULL) {
				remmina_file_manager_do_copy(
					g_build_path("/", remminadir, filename, NULL),
					g_build_path("/", g_get_user_data_dir(),
						     "unidesk", filename, NULL));
			}
		}
		g_free(remminadir), remminadir = NULL;
	}
	/* At last we make sure we use XDG_USER_DATA */
	if (remminadir != NULL)
		g_free(remminadir), remminadir = NULL;

	/* Unidesk: the connection database */
	remmina_db_init();
}

gint remmina_file_manager_iterate(GFunc func, gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile;
	gint items_count = 0;
	gchar **ids;
	gsize n = 0, i;

	remmina_db_init();
	ids = remmina_db_list_profiles(&n);
	for (i = 0; i < n; i++) {
		remminafile = remmina_file_load(ids[i]);
		if (remminafile) {
			(*func)(remminafile, user_data);
			remmina_file_free(remminafile);
			items_count++;
		}
	}
	g_strfreev(ids);
	return items_count;
}

static void remmina_file_manager_get_groups_cb(RemminaFile *remminafile, gpointer user_data)
{
	RemminaStringArray *array = (RemminaStringArray *)user_data;
	const gchar *group = remmina_file_get_string(remminafile, "group");

	if (group && remmina_string_array_find(array, group) < 0)
		remmina_string_array_add(array, group);
}

gchar *remmina_file_manager_get_groups(void)
{
	TRACE_CALL(__func__);
	RemminaStringArray *array;
	gchar *groups;

	array = remmina_string_array_new();
	remmina_file_manager_iterate((GFunc)remmina_file_manager_get_groups_cb, array);
	remmina_string_array_sort(array);
	groups = remmina_string_array_to_string(array);
	remmina_string_array_free(array);
	return groups;
}

static void remmina_file_manager_add_group(GNode *node, const gchar *group)
{
	TRACE_CALL(__func__);
	gint cmp;
	gchar *p1;
	gchar *p2;
	GNode *child;
	gboolean found;
	RemminaGroupData *data;

	if (node == NULL)
		return;

	if (group == NULL || group[0] == '\0')
		return;

	p1 = g_strdup(group);
	p2 = strchr(p1, '/');

	if (p2)
		*p2++ = '\0';

	found = FALSE;

	for (child = g_node_first_child(node); child; child = g_node_next_sibling(child)) {
		cmp = g_strcmp0(((RemminaGroupData *)child->data)->name, p1);

		if (cmp == 0) {
			found = TRUE;
			break;
		}

		if (cmp > 0)
			break;
	}

	if (!found) {
		data = g_new0(RemminaGroupData, 1);
		data->name = p1;
		if (node->data)
			data->group = g_strdup_printf("%s/%s", ((RemminaGroupData *)node->data)->group, p1);
		else
			data->group = g_strdup(p1);
		if (child)
			child = g_node_insert_data_before(node, child, data);
		else
			child = g_node_append_data(node, data);
	}
	remmina_file_manager_add_group(child, p2);

	if (found)
		g_free(p1);
}

static void remmina_file_manager_get_group_tree_cb(RemminaFile *remminafile, gpointer user_data)
{
	remmina_file_manager_add_group((GNode *)user_data, remmina_file_get_string(remminafile, "group"));
}

GNode *remmina_file_manager_get_group_tree(void)
{
	TRACE_CALL(__func__);
	GNode *root;

	root = g_node_new(NULL);
	remmina_file_manager_iterate((GFunc)remmina_file_manager_get_group_tree_cb, root);
	return root;
}

void remmina_file_manager_free_group_tree(GNode *node)
{
	TRACE_CALL(__func__);
	RemminaGroupData *data;
	GNode *child;

	if (!node)
		return;
	data = (RemminaGroupData *)node->data;
	if (data) {
		g_free(data->name);
		g_free(data->group);
		g_free(data);
		node->data = NULL;
	}
	for (child = g_node_first_child(node); child; child = g_node_next_sibling(child))
		remmina_file_manager_free_group_tree(child);
	g_node_unlink(node);
}

RemminaFile *remmina_file_manager_load_file(const gchar *filename)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile = NULL;
	RemminaFilePlugin *plugin;
	gchar *p;

	if (remmina_db_is_id(filename)) {
		remminafile = remmina_file_load(filename);
	} else if ((p = strrchr(filename, '.')) != NULL && g_strcmp0(p + 1, "remmina") == 0) {
		remminafile = remmina_file_load(filename);
	} else {
		plugin = remmina_plugin_manager_get_import_file_handler(filename);
		if (plugin)
			remminafile = plugin->import_func(plugin, filename);
	}
	return remminafile;
}

/* ------------------------------------------------------------------------- */
/* One-shot import of the legacy per-file Remmina profiles                    */
/* ------------------------------------------------------------------------- */

/* Read the 3DES key of a legacy remmina.pref so that inline encrypted passwords can be decrypted */
static gchar *remmina_file_manager_legacy_secret(void)
{
	gchar *path, *secret = NULL;
	GKeyFile *kf = g_key_file_new();

	path = g_build_filename(g_get_user_config_dir(), "remmina", "remmina.pref", NULL);
	if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
		secret = g_key_file_get_string(kf, "remmina_pref", "secret", NULL);
	g_key_file_free(kf);
	g_free(path);
	return secret;
}

static void remmina_file_manager_import_legacy_file(const gchar *path, RemminaSecretPlugin *keyring, gint *count)
{
	TRACE_CALL(__func__);
	GKeyFile *kf;
	gchar **keys, *proto, *id, *statepath, *basename;
	gsize n = 0, i;
	RemminaProtocolPlugin *plugin;
	RemminaFile *shell;
	GHashTable *settings;
	GKeyFile *state;

	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL) || !g_key_file_has_key(kf, "remmina", "name", NULL)) {
		g_key_file_free(kf);
		return;
	}
	proto = g_key_file_get_string(kf, "remmina", "protocol", NULL);
	plugin = proto ? (RemminaProtocolPlugin *)remmina_plugin_manager_get_plugin(REMMINA_PLUGIN_TYPE_PROTOCOL, proto) : NULL;
	if (!plugin) {
		REMMINA_WARNING("Legacy profile %s uses the protocol %s which is not available in UniDeskBrowser, skipped", path, proto ? proto : "(none)");
		g_free(proto);
		g_key_file_free(kf);
		return;
	}

	id = remmina_db_new_id();
	/* A RemminaFile shell with the legacy file name: the keyring stores secrets by file name */
	shell = remmina_file_new();
	remmina_file_set_filename(shell, path);

	settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	keys = g_key_file_get_keys(kf, "remmina", &n, NULL);
	for (i = 0; i < n; i++) {
		gchar *v = g_key_file_get_string(kf, "remmina", keys[i], NULL);

		if (remmina_plugin_manager_is_encrypted_setting(plugin, keys[i])) {
			gchar *secret = NULL;
			if (g_strcmp0(v, ".") == 0) {
				if (keyring && keyring->is_service_available(keyring))
					secret = keyring->get_password(keyring, shell, keys[i]);
			} else if (v && v[0]) {
				secret = remmina_crypt_decrypt(v);
			}
			if (secret && secret[0]) {
				remmina_vault_put(id, keys[i], secret);
				g_hash_table_insert(settings, g_strdup(keys[i]), g_strdup("."));
			} else {
				g_hash_table_insert(settings, g_strdup(keys[i]), g_strdup(""));
			}
			g_free(secret);
		} else if (g_strcmp0(keys[i], "resolution") == 0) {
			gint w, h;
			if (v && remmina_public_split_resolution_string(v, &w, &h)) {
				g_hash_table_insert(settings, g_strdup("resolution_width"), g_strdup_printf("%i", w));
				g_hash_table_insert(settings, g_strdup("resolution_height"), g_strdup_printf("%i", h));
			}
		} else {
			g_hash_table_insert(settings, g_strdup(keys[i]), g_strdup(v ? v : ""));
		}
		g_free(v);
	}
	g_strfreev(keys);
	remmina_db_profile_save(id, settings);
	g_hash_table_destroy(settings);

	/* Legacy state file: <cache>/remmina/<file>.state */
	basename = g_path_get_basename(path);
	statepath = g_build_filename(g_get_user_cache_dir(), "remmina", NULL);
	{
		gchar *sp = g_strdup_printf("%s/%s.state", statepath, basename);
		state = g_key_file_new();
		if (g_key_file_load_from_file(state, sp, G_KEY_FILE_NONE, NULL)) {
			gchar **skeys = g_key_file_get_keys(state, "Remmina Connection States", &n, NULL);
			for (i = 0; i < n; i++) {
				gchar *sv = g_key_file_get_string(state, "Remmina Connection States", skeys[i], NULL);
				remmina_db_state_set(id, skeys[i], sv);
				g_free(sv);
			}
			g_strfreev(skeys);
			GStatBuf st;
			if (g_stat(sp, &st) == 0)
				remmina_db_profile_set_last_used(id, (gint64)st.st_mtime);
		}
		g_key_file_free(state);
		g_free(sp);
	}
	remmina_db_state_set(id, "legacy_file", path);
	g_free(statepath);
	g_free(basename);

	remmina_file_free(shell);
	g_free(proto);
	g_free(id);
	g_key_file_free(kf);
	(*count)++;
}

gint remmina_file_manager_import_legacy(void)
{
	TRACE_CALL(__func__);
	gchar *marker, *secret, *saved_secret = NULL;
	const gchar *dirs[2];
	RemminaSecretPlugin *keyring;
	gint count = 0, d;

	remmina_db_init();
	marker = remmina_db_meta_get("legacy_imported");
	if (marker) {
		g_free(marker);
		return 0;
	}

	/* Collect the legacy profile files first: the keyring is only contacted when there is something to import */
	GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
	dirs[0] = g_build_filename(g_get_user_data_dir(), "remmina", NULL);
	dirs[1] = g_build_filename(g_get_home_dir(), ".remmina", NULL);
	for (d = 0; d < 2; d++) {
		GDir *dir = g_dir_open(dirs[d], 0, NULL);
		const gchar *name;
		if (!dir)
			continue;
		while ((name = g_dir_read_name(dir)) != NULL) {
			if (!g_str_has_suffix(name, ".remmina"))
				continue;
			g_ptr_array_add(files, g_build_filename(dirs[d], name, NULL));
		}
		g_dir_close(dir);
	}
	g_free((gchar *)dirs[0]);
	g_free((gchar *)dirs[1]);

	if (files->len > 0) {
		guint i;

		/* The GNOME keyring plugin (if built and available) still holds the old passwords */
		keyring = (RemminaSecretPlugin *)remmina_plugin_manager_get_plugin(REMMINA_PLUGIN_TYPE_SECRET, "glibsecret");
		if (keyring && !keyring->init(keyring))
			keyring = NULL;

		/* Inline encrypted passwords use the 3DES key of the legacy preferences */
		secret = remmina_file_manager_legacy_secret();
		if (secret && g_strcmp0(secret, remmina_pref.secret) != 0) {
			saved_secret = remmina_pref.secret;
			remmina_pref.secret = secret;
		}

		for (i = 0; i < files->len; i++)
			remmina_file_manager_import_legacy_file((const gchar *)g_ptr_array_index(files, i), keyring, &count);

		if (saved_secret)
			remmina_pref.secret = saved_secret;
		g_free(secret);
	}
	g_ptr_array_free(files, TRUE);

	{
		GDateTime *now = g_date_time_new_now_utc();
		gchar *when = g_date_time_format(now, "%FT%TZ");
		remmina_db_meta_set("legacy_imported", when);
		g_free(when);
		g_date_time_unref(now);
	}
	if (count > 0)
		REMMINA_INFO("%d legacy Remmina profile(s) imported into the connection database", count);
	return count;
}

/* "Best" is the default quality of Unidesk (wallpaper, themes and ClearType, like GNOME Connections).
 * Profiles imported from Remmina, or saved by early Unidesk builds, carry quality=0: "Poor", the
 * first entry of Remmina's list, which turns the remote wallpaper and themes off. Switch them once;
 * "Medium" and "Good" were never defaults, so they are a user's choice and stay. */
gint remmina_file_manager_migrate_rdp_quality(void)
{
	TRACE_CALL(__func__);
	gchar *marker, **ids;
	gint count = 0;

	remmina_db_init();
	marker = remmina_db_meta_get("rdp_quality_best");
	if (marker) {
		g_free(marker);
		return 0;
	}
	ids = remmina_db_list_profiles(NULL);
	for (gint i = 0; ids && ids[i]; i++) {
		GHashTable *settings = remmina_db_profile_load(ids[i]);
		if (!settings)
			continue;
		if (g_strcmp0(g_hash_table_lookup(settings, "protocol"), "RDP") == 0 &&
		    g_strcmp0(g_hash_table_lookup(settings, "quality"), "0") == 0) {
			g_hash_table_replace(settings, g_strdup("quality"), g_strdup("9"));
			if (remmina_db_profile_save(ids[i], settings))
				count++;
		}
		g_hash_table_destroy(settings);
	}
	g_strfreev(ids);
	remmina_db_meta_set("rdp_quality_best", "1");
	if (count > 0)
		REMMINA_INFO("%d RDP profile(s) switched from quality Poor to Best", count);
	return count;
}
