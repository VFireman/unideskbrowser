/*
 * Remmina - The GTK+ Remote Desktop Client
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
 * @file remmina_theme.c
 * @brief Force the dark GTK theme variant and the Unidesk palette.
 *
 * Unidesk always runs with the dark variant of the current GTK theme: there is
 * no light/dark preference and no desktop (portal) colour-scheme tracking.
 * On top of Adwaita dark it loads data/style/unidesk-dark.css: the same Adwaita theme
 * compiled with the Unidesk palette (Breeze Dark as in LACT, packaging/theme/), so the
 * widget shapes stay Adwaita and only the colours change.
 */

#include "config.h"
#include <gtk/gtk.h>

#include "remmina_theme.h"
#include "remmina/remmina_trace_calls.h"

void remmina_theme_apply(void)
{
	TRACE_CALL(__func__);
	GtkSettings *settings = gtk_settings_get_default();

	if (!settings)
		return;

	g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

	/* The palette is a whole theme. GTK 3 adds the theme chosen in the settings (Adwaita) with
	 * the SETTINGS priority, not THEME: one step above it the palette wins for every rule and
	 * still yields to the application CSS and the user's gtk.css */
	static GtkCssProvider *palette = NULL;
	if (!palette) {
		/* Read the bytes directly: gtk_css_provider_load_from_resource() goes through a GFile,
		 * which folds "src/../data" into "data" and misses our resource path */
		GBytes *css = g_resources_lookup_data("/org/unidesk/Unidesk/src/../data/style/unidesk-dark.css",
						      G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
		palette = gtk_css_provider_new();
		if (css) {
			gtk_css_provider_load_from_data(palette, g_bytes_get_data(css, NULL), g_bytes_get_size(css), NULL);
			g_bytes_unref(css);
		}
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(palette),
							  GTK_STYLE_PROVIDER_PRIORITY_SETTINGS + 1);
	}
	/* GTK activates the first menu bar of a window on F10 (gtk-menu-bar-accel) before the
	 * focused widget sees the key; the connection list keeps a hidden GtkMenuBar for its
	 * popup menus, so F10 vanished from terminals and remote desktops and the following
	 * keystrokes went into an invisible menu. Function keys belong to the session. */
	g_object_set(settings, "gtk-menu-bar-accel", "", NULL);
}

void remmina_theme_init(void)
{
	TRACE_CALL(__func__);
	remmina_theme_apply();
}
