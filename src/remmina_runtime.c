/*
 * Unidesk - relocatable runtime directories (AppImage support)
 *
 * The directories are compiled in as absolute paths under CMAKE_INSTALL_PREFIX.
 * When the binary is started from an AppImage (AppRun exports APPDIR) or from
 * any relocated install tree, the compiled paths are prefixed with that root.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "remmina_runtime.h"

static const gchar *compiled_dirs[REMMINA_RUNTIME_DIR_LAST] = {
	REMMINA_RUNTIME_DATADIR,
	REMMINA_RUNTIME_PLUGINDIR,
	REMMINA_RUNTIME_UIDIR,
	REMMINA_RUNTIME_THEMEDIR,
	REMMINA_RUNTIME_DATADIR,	/* RES: not exported by config.h, same prefix as the data dir */
	REMMINA_RUNTIME_LOCALEDIR,
	REMMINA_RUNTIME_EXTERNAL_TOOLS_DIR,
	REMMINA_RUNTIME_TERM_CS_DIR
};

static gchar *runtime_dirs[REMMINA_RUNTIME_DIR_LAST];
static gchar *runtime_root = NULL;
static gboolean runtime_initialized = FALSE;

/* Try to find a root R such that R + compiled UI dir exists */
static gchar *remmina_runtime_detect_root(void)
{
	const gchar *env;
	gchar *exe, *bindir, *root;
	gchar *probe;

	/* 1. AppImage (AppRun) or explicit override */
	env = g_getenv("UNIDESK_APPDIR");
	if (!env || !env[0])
		env = g_getenv("APPDIR");
	if (env && env[0]) {
		probe = g_build_filename(env, REMMINA_RUNTIME_PLUGINDIR, NULL);
		if (g_file_test(probe, G_FILE_TEST_IS_DIR)) {
			g_free(probe);
			return g_strdup(env);
		}
		g_free(probe);
	}

	/* 2. Compiled prefix is fine as it is (the UI dir is not installed, probe the plugin dir) */
	if (g_file_test(REMMINA_RUNTIME_PLUGINDIR, G_FILE_TEST_IS_DIR))
		return g_strdup("");

	/* 3. Relocated tree: <root>/usr/bin/<exe> -> <root> + compiled paths */
	exe = g_file_read_link("/proc/self/exe", NULL);
	if (exe) {
		bindir = g_path_get_dirname(exe);
		g_free(exe);
		/* walk up a few levels looking for the UI dir */
		root = g_strdup(bindir);
		g_free(bindir);
		for (int i = 0; i < 4 && root; i++) {
			gchar *parent = g_path_get_dirname(root);
			probe = g_build_filename(parent, REMMINA_RUNTIME_PLUGINDIR, NULL);
			if (g_file_test(probe, G_FILE_TEST_IS_DIR)) {
				g_free(probe);
				g_free(root);
				return parent;
			}
			g_free(probe);
			g_free(root);
			if (g_strcmp0(parent, "/") == 0) {
				g_free(parent);
				break;
			}
			root = parent;
		}
	}
	return g_strdup("");
}

static void remmina_runtime_init(void)
{
	gint i;

	if (runtime_initialized)
		return;
	runtime_initialized = TRUE;
	runtime_root = remmina_runtime_detect_root();
	for (i = 0; i < REMMINA_RUNTIME_DIR_LAST; i++) {
		if (runtime_root[0])
			runtime_dirs[i] = g_build_filename(runtime_root, compiled_dirs[i], NULL);
		else
			runtime_dirs[i] = g_strdup(compiled_dirs[i]);
	}
}

const gchar *remmina_runtime_dir(RemminaRuntimeDir kind)
{
	remmina_runtime_init();
	if (kind < 0 || kind >= REMMINA_RUNTIME_DIR_LAST)
		return "";
	return runtime_dirs[kind];
}

const gchar *remmina_runtime_root(void)
{
	remmina_runtime_init();
	return runtime_root;
}
