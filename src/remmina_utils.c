/*
 * Remmina - The GTK+ Remote Desktop Client
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

/**
 * General utility functions, non-GTK related.
 */

#include <stdlib.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "remmina/remmina_trace_calls.h"
#include "remmina_utils.h"

/** Returns @c TRUE if @a ptr is @c NULL or @c *ptr is @c FALSE. */
#define EMPTY(ptr) \
	(!(ptr) || !*(ptr))

static gint remmina_utils_strpos(const gchar *haystack, const gchar *needle)
{
	TRACE_CALL(__func__);
	const gchar *sub;

	if (!*needle)
		return -1;

	sub = strstr(haystack, needle);
	if (!sub)
		return -1;

	return sub - haystack;
}

// Determines if we're in a flatpak environment and prepends to the cmd if so
// The original value passed in will be freed
gchar* remmina_utils_get_flatpak_command(gchar *cmd){
	gchar* ret = NULL;
	gchar *flatpak_info = g_build_filename(g_get_user_runtime_dir(), "flatpak-info", NULL);

	if (g_file_test(flatpak_info, G_FILE_TEST_EXISTS)) {
		ret = g_strconcat("flatpak-spawn --host ", cmd, NULL);
	} 
	else{
		ret = g_strdup(cmd);
	}
	g_free(flatpak_info);
	g_free(cmd);
	return ret;
}

/* end can be -1 for haystack->len.
 * returns: position of found text or -1.
 * (C) Taken from geany */
gint remmina_utils_string_find(GString *haystack, gint start, gint end, const gchar *needle)
{
	TRACE_CALL(__func__);
	gint pos;

	g_return_val_if_fail(haystack != NULL, -1);
	if (haystack->len == 0)
		return -1;

	g_return_val_if_fail(start >= 0, -1);
	if (start >= (gint)haystack->len)
		return -1;

	g_return_val_if_fail(!EMPTY(needle), -1);

	if (end < 0)
		end = haystack->len;

	pos = remmina_utils_strpos(haystack->str + start, needle);
	if (pos == -1)
		return -1;

	pos += start;
	if (pos >= end)
		return -1;
	return pos;
}

/* Replaces @len characters from offset @a pos.
 * len can be -1 to replace the remainder of @a str.
 * returns: pos + strlen(replace).
 * (C) Taken from geany */
gint remmina_utils_string_replace(GString *str, gint pos, gint len, const gchar *replace)
{
	TRACE_CALL(__func__);
	g_string_erase(str, pos, len);
	if (replace) {
		g_string_insert(str, pos, replace);
		pos += strlen(replace);
	}
	return pos;
}

/**
 * Replaces all occurrences of @a needle in @a haystack with @a replace.
 *
 * @param haystack The input string to operate on. This string is modified in place.
 * @param needle The string which should be replaced.
 * @param replace The replacement for @a needle.
 *
 * @return Number of replacements made.
 **/
guint remmina_utils_string_replace_all(GString *haystack, const gchar *needle, const gchar *replace)
{
	TRACE_CALL(__func__);
	guint count = 0;
	gint pos = 0;
	gsize needle_length = strlen(needle);

	while (1) {
		pos = remmina_utils_string_find(haystack, pos, -1, needle);

		if (pos == -1)
			break;

		pos = remmina_utils_string_replace(haystack, pos, needle_length, replace);
		count++;
	}
	return count;
}

gint remmina_get_monitor_num(GdkDisplay* d, GdkMonitor* m) {
	for (int i = 0 ; i < gdk_display_get_n_monitors(d) ; i++) {
		if (m == gdk_display_get_monitor(d, i))
			return i;
	}
	return 0;
}

gint remmina_get_primary_monitor_num() {
	GdkDisplay* d = gdk_display_get_default();
	GdkMonitor* m = gdk_display_get_primary_monitor(d);

	return remmina_get_monitor_num(d, m);
}