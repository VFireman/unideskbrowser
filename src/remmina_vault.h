/*
 * Unidesk - encrypted password vault (built-in secret plugin)
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
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Register the vault as a RemminaSecretPlugin (call before remmina_plugin_manager_init loads plugins) */
void remmina_vault_plugin_register(void);

/* TRUE when the vault content is readable (key known) */
gboolean remmina_vault_is_unlocked(void);
/* TRUE when the vault is protected by a master password (otherwise by a key kept in the preferences) */
gboolean remmina_vault_has_master_password(void);
/* Derive the key from the master password and open the vault; FALSE when the password is wrong */
gboolean remmina_vault_unlock_with_password(const gchar *password);
/* Set (password != NULL) or remove (NULL) the master password; the vault must be unlocked */
gboolean remmina_vault_set_master_password(const gchar *password);
/* Forget the key (the vault stays on disk) */
void remmina_vault_lock(void);
/* Ask the master password in a modal dialog until it is right or the user gives up */
gboolean remmina_vault_prompt_unlock(GtkWindow *parent);
/* Number of stored secrets (for the preferences / diagnostics) */
gint remmina_vault_count(void);
/* Path of the vault file */
const gchar *remmina_vault_get_path(void);
/* Store a secret for a profile identifier (used by the migration from legacy profiles) */
void remmina_vault_put(const gchar *profile_id, const gchar *key, const gchar *value);
/* Rename every secret of a profile identifier (migration: legacy file name -> database id) */
void remmina_vault_rename_profile(const gchar *old_id, const gchar *new_id);

G_END_DECLS
