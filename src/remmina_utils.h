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
 *  @file: remmina_utils.h
 *  General utility functions, non-GTK related.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <gdk/gdk.h>

/* https://github.com/hboetes/mg/issues/7#issuecomment-475869095 */
#if defined(__APPLE__) || defined(__NetBSD__)
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#define st_mtim st_mtimespec
#endif

G_BEGIN_DECLS
gchar *remmina_utils_get_flatpak_command(gchar* cmd);
gint remmina_utils_string_find(GString *haystack, gint start, gint end, const gchar *needle);
gint remmina_utils_string_replace(GString *str, gint pos, gint len, const gchar *replace);

guint remmina_utils_string_replace_all(GString *haystack, const gchar *needle, const gchar *replace);

gint remmina_get_monitor_num(GdkDisplay* d, GdkMonitor* m);
gint remmina_get_primary_monitor_num();
G_END_DECLS
