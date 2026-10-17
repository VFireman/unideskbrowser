/*
 * Unidesk - relocatable runtime directories (AppImage support)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	REMMINA_RUNTIME_DIR_DATA = 0,           /* <prefix>/share                      */
	REMMINA_RUNTIME_DIR_PLUGINS,            /* <prefix>/lib/unidesk/plugins        */
	REMMINA_RUNTIME_DIR_UI,                 /* <prefix>/share/unidesk/ui           */
	REMMINA_RUNTIME_DIR_THEME,              /* <prefix>/share/unidesk/theme        */
	REMMINA_RUNTIME_DIR_RES,                /* <prefix>/share/unidesk/res          */
	REMMINA_RUNTIME_DIR_LOCALE,             /* <prefix>/share/locale               */
	REMMINA_RUNTIME_DIR_EXTERNAL_TOOLS,     /* <prefix>/share/unidesk/external_tools */
	REMMINA_RUNTIME_DIR_TERM_CS,            /* <prefix>/share/unidesk/theme        */
	REMMINA_RUNTIME_DIR_LAST
} RemminaRuntimeDir;

/**
 * Returns the directory of the given kind, relocated when the application does
 * not run from its compiled prefix (AppImage: $APPDIR, or any relocated tree
 * detected from /proc/self/exe). The returned string is owned by the library.
 */
const gchar *remmina_runtime_dir(RemminaRuntimeDir kind);

/** The detected relocation root ("" when running from the compiled prefix). */
const gchar *remmina_runtime_root(void);

G_END_DECLS
