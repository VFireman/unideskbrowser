/*
 * Unidesk - Telnet protocol plugin (built-in)
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

G_BEGIN_DECLS

/* Register the built-in TELNET protocol plugin (no-op when VTE is not available) */
void remmina_telnet_plugin_register(void);

G_END_DECLS
