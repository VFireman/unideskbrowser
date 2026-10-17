/*
 * Unidesk - the scale of the windows drawn through XWayland
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

#include <gio/gio.h>

G_BEGIN_DECLS

/* Before GTK opens the display: GDK_SCALE (and GDK_DPI_SCALE) fitted to the screens */
void remmina_scale_adapt(void);
/* The programs we start get GDK_SCALE and GDK_DPI_SCALE as the desktop gave them */
void remmina_scale_launch_context(GAppLaunchContext *ctx);

G_END_DECLS
