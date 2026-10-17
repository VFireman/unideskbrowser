/*
 * Unidesk - encrypted password vault (built-in secret plugin)
 *
 * All secrets (passwords, passphrases, gateway passwords…) of every connection
 * profile live in one file, encrypted with libsodium:
 *
 *   <config dir>/vault.db = "UDVLT001" | flags(1) | salt(16) | nonce(24) | secretbox(keyfile text)
 *
 * The decrypted content is a GKeyFile: one group per profile identifier, one
 * key per secret setting name.
 *
 * The encryption key is either derived (Argon2id) from a master password, or
 * a random key kept base64 encoded in the preferences file ("vault_key") when
 * no master password has been set. The master password is the "primary
 * password" of the preferences, so a single prompt unlocks everything.
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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <sodium.h>
#include <string.h>

#include "remmina_pref.h"
#include "remmina_file.h"
#include "remmina_log.h"
#include "remmina_main.h"
#include "remmina_plugin_manager.h"
#include "remmina_vault.h"
#include "remmina/remmina_trace_calls.h"

#define VAULT_MAGIC		"UDVLT001"
#define VAULT_MAGIC_LEN		8
#define VAULT_SALT_LEN		crypto_pwhash_SALTBYTES
#define VAULT_NONCE_LEN		crypto_secretbox_NONCEBYTES
#define VAULT_KEY_LEN		crypto_secretbox_KEYBYTES
#define VAULT_HEADER_LEN	(VAULT_MAGIC_LEN + 1 + VAULT_SALT_LEN + VAULT_NONCE_LEN)
#define VAULT_FLAG_MASTER	0x01
#define VAULT_FILE_NAME		"vault.db"
#define VAULT_PREF_KEY		"vault_key"
#define VAULT_CONFIG_SUBDIR	"unidesk"

typedef struct _RemminaVault {
	gchar *		path;
	GKeyFile *	kf;			/* decrypted content, NULL while locked */
	guchar		key[VAULT_KEY_LEN];
	gboolean	have_key;
	guchar		salt[VAULT_SALT_LEN];
	guint8		flags;
	gboolean	file_exists;
	gboolean	prompt_cancelled;
} RemminaVault;

static RemminaVault vault;

/* ------------------------------------------------------------------------- */
/* Low level: key, file                                                       */
/* ------------------------------------------------------------------------- */

static const gchar *remmina_vault_path(void)
{
	if (!vault.path)
		vault.path = g_build_filename(g_get_user_config_dir(), VAULT_CONFIG_SUBDIR, VAULT_FILE_NAME, NULL);
	return vault.path;
}

const gchar *remmina_vault_get_path(void)
{
	return remmina_vault_path();
}

static gboolean remmina_vault_derive_key(const gchar *password, const guchar *salt, guchar *out)
{
	TRACE_CALL(__func__);
	if (!password)
		return FALSE;
	if (crypto_pwhash(out, VAULT_KEY_LEN, password, strlen(password), salt,
			  crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
			  crypto_pwhash_ALG_ARGON2ID13) != 0) {
		REMMINA_WARNING("Key derivation failed (out of memory?)");
		return FALSE;
	}
	return TRUE;
}

/* Key kept in the preferences when there is no master password */
static gboolean remmina_vault_load_pref_key(guchar *out)
{
	gchar *b64;
	gsize len = 0;
	guchar *raw;

	b64 = remmina_pref_get_value(VAULT_PREF_KEY);
	if (!b64 || !b64[0]) {
		g_free(b64);
		return FALSE;
	}
	raw = g_base64_decode(b64, &len);
	g_free(b64);
	if (!raw || len != VAULT_KEY_LEN) {
		g_free(raw);
		return FALSE;
	}
	memcpy(out, raw, VAULT_KEY_LEN);
	sodium_memzero(raw, len);
	g_free(raw);
	return TRUE;
}

static void remmina_vault_store_pref_key(const guchar *key)
{
	gchar *b64 = g_base64_encode(key, VAULT_KEY_LEN);

	remmina_pref_set_value(VAULT_PREF_KEY, b64);
	g_free(b64);
}

static void remmina_vault_clear_pref_key(void)
{
	remmina_pref_set_value(VAULT_PREF_KEY, "");
}

/* Read the header of the vault file; returns FALSE if there is no (valid) file */
static gboolean remmina_vault_read_header(void)
{
	gchar *data = NULL;
	gsize len = 0;

	vault.file_exists = FALSE;
	if (!g_file_get_contents(remmina_vault_path(), &data, &len, NULL))
		return FALSE;
	if (len < VAULT_HEADER_LEN || memcmp(data, VAULT_MAGIC, VAULT_MAGIC_LEN) != 0) {
		REMMINA_WARNING("Vault file %s is not a valid UniDeskBrowser vault", remmina_vault_path());
		g_free(data);
		return FALSE;
	}
	vault.flags = (guint8)data[VAULT_MAGIC_LEN];
	memcpy(vault.salt, data + VAULT_MAGIC_LEN + 1, VAULT_SALT_LEN);
	vault.file_exists = TRUE;
	g_free(data);
	return TRUE;
}

/* Decrypt the file with the given key into a GKeyFile */
static GKeyFile *remmina_vault_decrypt_file(const guchar *key)
{
	gchar *data = NULL;
	gsize len = 0;
	const guchar *nonce, *cipher;
	gsize clen;
	guchar *plain;
	GKeyFile *kf;
	GError *err = NULL;

	if (!g_file_get_contents(remmina_vault_path(), &data, &len, NULL))
		return NULL;
	if (len < VAULT_HEADER_LEN + crypto_secretbox_MACBYTES) {
		g_free(data);
		return NULL;
	}
	nonce = (const guchar *)data + VAULT_MAGIC_LEN + 1 + VAULT_SALT_LEN;
	cipher = (const guchar *)data + VAULT_HEADER_LEN;
	clen = len - VAULT_HEADER_LEN;
	plain = g_malloc0(clen - crypto_secretbox_MACBYTES + 1);
	if (crypto_secretbox_open_easy(plain, cipher, clen, nonce, key) != 0) {
		g_free(plain);
		g_free(data);
		return NULL;
	}
	g_free(data);
	kf = g_key_file_new();
	if (!g_key_file_load_from_data(kf, (const gchar *)plain, clen - crypto_secretbox_MACBYTES, G_KEY_FILE_NONE, &err)) {
		REMMINA_WARNING("Vault content cannot be parsed: %s", err ? err->message : "");
		g_clear_error(&err);
		g_key_file_free(kf);
		kf = NULL;
	}
	/* plain holds clen - MACBYTES bytes plus the terminating NUL: wipe exactly that much */
	sodium_memzero(plain, clen - crypto_secretbox_MACBYTES + 1);
	g_free(plain);
	return kf;
}

/* Encrypt the current content with the current key and write the file atomically */
static gboolean remmina_vault_save(void)
{
	TRACE_CALL(__func__);
	gchar *plain;
	gsize plen = 0;
	guchar nonce[VAULT_NONCE_LEN];
	GByteArray *out;
	guchar *cipher;
	gsize clen;
	gchar *dir, *tmp;
	GError *err = NULL;
	gboolean ok;

	if (!vault.kf || !vault.have_key)
		return FALSE;

	plain = g_key_file_to_data(vault.kf, &plen, NULL);
	if (!plain)
		return FALSE;

	randombytes_buf(nonce, sizeof(nonce));
	clen = plen + crypto_secretbox_MACBYTES;
	cipher = g_malloc(clen);
	crypto_secretbox_easy(cipher, (const guchar *)plain, plen, nonce, vault.key);
	sodium_memzero(plain, plen);
	g_free(plain);

	out = g_byte_array_sized_new(VAULT_HEADER_LEN + clen);
	g_byte_array_append(out, (const guint8 *)VAULT_MAGIC, VAULT_MAGIC_LEN);
	g_byte_array_append(out, &vault.flags, 1);
	g_byte_array_append(out, vault.salt, VAULT_SALT_LEN);
	g_byte_array_append(out, nonce, VAULT_NONCE_LEN);
	g_byte_array_append(out, cipher, clen);
	g_free(cipher);

	dir = g_path_get_dirname(remmina_vault_path());
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);

	tmp = g_strconcat(remmina_vault_path(), ".tmp", NULL);
	ok = g_file_set_contents(tmp, (const gchar *)out->data, out->len, &err);
	if (ok) {
		g_chmod(tmp, 0600);
		if (g_rename(tmp, remmina_vault_path()) != 0) {
			REMMINA_WARNING("Cannot replace the vault file %s", remmina_vault_path());
			ok = FALSE;
		}
	} else {
		REMMINA_WARNING("Cannot write the vault file: %s", err ? err->message : "");
		g_clear_error(&err);
	}
	g_free(tmp);
	g_byte_array_free(out, TRUE);
	vault.file_exists = ok;
	return ok;
}

static void remmina_vault_set_key(const guchar *key)
{
	memcpy(vault.key, key, VAULT_KEY_LEN);
	vault.have_key = TRUE;
}

/* Create a brand new empty vault protected by a random key stored in the preferences */
static gboolean remmina_vault_create_new(void)
{
	TRACE_CALL(__func__);
	guchar key[VAULT_KEY_LEN];

	randombytes_buf(key, sizeof(key));
	randombytes_buf(vault.salt, sizeof(vault.salt));
	vault.flags = 0;
	remmina_vault_set_key(key);
	remmina_vault_store_pref_key(key);
	sodium_memzero(key, sizeof(key));
	if (vault.kf)
		g_key_file_free(vault.kf);
	vault.kf = g_key_file_new();
	g_key_file_set_integer(vault.kf, "vault", "version", 1);
	return remmina_vault_save();
}

/* ------------------------------------------------------------------------- */
/* Public state management                                                     */
/* ------------------------------------------------------------------------- */

gboolean remmina_vault_is_unlocked(void)
{
	return vault.kf != NULL;
}

gboolean remmina_vault_has_master_password(void)
{
	return vault.file_exists && (vault.flags & VAULT_FLAG_MASTER);
}

void remmina_vault_lock(void)
{
	TRACE_CALL(__func__);
	if (vault.kf) {
		g_key_file_free(vault.kf);
		vault.kf = NULL;
	}
	sodium_memzero(vault.key, sizeof(vault.key));
	vault.have_key = FALSE;
}

gboolean remmina_vault_unlock_with_password(const gchar *password)
{
	TRACE_CALL(__func__);
	guchar key[VAULT_KEY_LEN];
	GKeyFile *kf;

	if (vault.kf)
		return TRUE;
	if (!vault.file_exists && !remmina_vault_read_header())
		return FALSE;
	if (!(vault.flags & VAULT_FLAG_MASTER)) {
		/* No master password: the key comes from the preferences */
		if (!remmina_vault_load_pref_key(key))
			return FALSE;
	} else if (!remmina_vault_derive_key(password, vault.salt, key)) {
		return FALSE;
	}
	kf = remmina_vault_decrypt_file(key);
	if (!kf) {
		sodium_memzero(key, sizeof(key));
		return FALSE;
	}
	remmina_vault_set_key(key);
	sodium_memzero(key, sizeof(key));
	vault.kf = kf;
	vault.prompt_cancelled = FALSE;
	REMMINA_DEBUG("Vault unlocked");
	return TRUE;
}

gboolean remmina_vault_set_master_password(const gchar *password)
{
	TRACE_CALL(__func__);
	guchar key[VAULT_KEY_LEN];

	if (!vault.kf) {
		REMMINA_WARNING("The vault must be unlocked before changing its master password");
		return FALSE;
	}
	if (password && password[0]) {
		randombytes_buf(vault.salt, sizeof(vault.salt));
		if (!remmina_vault_derive_key(password, vault.salt, key))
			return FALSE;
		vault.flags |= VAULT_FLAG_MASTER;
		remmina_vault_set_key(key);
		sodium_memzero(key, sizeof(key));
		remmina_vault_clear_pref_key();
	} else {
		randombytes_buf(key, sizeof(key));
		randombytes_buf(vault.salt, sizeof(vault.salt));
		vault.flags &= ~VAULT_FLAG_MASTER;
		remmina_vault_set_key(key);
		remmina_vault_store_pref_key(key);
		sodium_memzero(key, sizeof(key));
	}
	return remmina_vault_save();
}

gint remmina_vault_count(void)
{
	gchar **groups;
	gsize n = 0, i;
	gint count = 0;

	if (!vault.kf)
		return 0;
	groups = g_key_file_get_groups(vault.kf, &n);
	for (i = 0; i < n; i++) {
		gsize nk = 0;
		gchar **keys;
		if (g_strcmp0(groups[i], "vault") == 0)
			continue;
		keys = g_key_file_get_keys(vault.kf, groups[i], &nk, NULL);
		count += (gint)nk;
		g_strfreev(keys);
	}
	g_strfreev(groups);
	return count;
}

/* Modal prompt for the master password */
gboolean remmina_vault_prompt_unlock(GtkWindow *parent)
{
	TRACE_CALL(__func__);
	GtkWidget *dialog, *box, *label, *entry;
	gint attempts;
	gboolean ok = FALSE;

	if (vault.kf)
		return TRUE;
	if (!remmina_vault_has_master_password())
		return remmina_vault_unlock_with_password(NULL);
	if (vault.prompt_cancelled)
		return FALSE;

	dialog = gtk_dialog_new_with_buttons(_("Unlock the password vault"), parent,
					     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					     _("_Cancel"), GTK_RESPONSE_CANCEL,
					     _("_Unlock"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_box_set_spacing(GTK_BOX(box), 8);
	label = gtk_label_new(_("Enter the master password to access the saved passwords."));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	entry = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_widget_show_all(box);

	for (attempts = 0; attempts < 3 && !ok; attempts++) {
		if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
			vault.prompt_cancelled = TRUE;
			break;
		}
		ok = remmina_vault_unlock_with_password(gtk_entry_get_text(GTK_ENTRY(entry)));
		if (!ok) {
			gtk_label_set_text(GTK_LABEL(label), _("Wrong master password, try again."));
			gtk_entry_set_text(GTK_ENTRY(entry), "");
		}
	}
	gtk_widget_destroy(dialog);
	return ok;
}

/* ------------------------------------------------------------------------- */
/* Secrets                                                                     */
/* ------------------------------------------------------------------------- */

void remmina_vault_put(const gchar *profile_id, const gchar *key, const gchar *value)
{
	TRACE_CALL(__func__);
	if (!vault.kf || !profile_id || !key)
		return;
	if (value && value[0])
		g_key_file_set_string(vault.kf, profile_id, key, value);
	else
		g_key_file_remove_key(vault.kf, profile_id, key, NULL);
	remmina_vault_save();
}

void remmina_vault_rename_profile(const gchar *old_id, const gchar *new_id)
{
	TRACE_CALL(__func__);
	gchar **keys;
	gsize n = 0, i;

	if (!vault.kf || !old_id || !new_id || !g_key_file_has_group(vault.kf, old_id))
		return;
	keys = g_key_file_get_keys(vault.kf, old_id, &n, NULL);
	for (i = 0; i < n; i++) {
		gchar *v = g_key_file_get_string(vault.kf, old_id, keys[i], NULL);
		if (v) {
			g_key_file_set_string(vault.kf, new_id, keys[i], v);
			sodium_memzero(v, strlen(v));
			g_free(v);
		}
	}
	g_strfreev(keys);
	g_key_file_remove_group(vault.kf, old_id, NULL);
	remmina_vault_save();
}

/* ------------------------------------------------------------------------- */
/* RemminaSecretPlugin interface                                               */
/* ------------------------------------------------------------------------- */

static gboolean remmina_vault_plugin_init(RemminaSecretPlugin *instance)
{
	TRACE_CALL(__func__);
	if (sodium_init() < 0) {
		REMMINA_CRITICAL("libsodium cannot be initialised, the vault is unavailable");
		return FALSE;
	}
	if (remmina_vault_read_header()) {
		if (!(vault.flags & VAULT_FLAG_MASTER)) {
			if (!remmina_vault_unlock_with_password(NULL)) {
				gchar *broken = g_strconcat(remmina_vault_path(), ".broken", NULL);
				REMMINA_WARNING("The vault key is missing from the preferences: the vault is moved to %s and recreated", broken);
				g_rename(remmina_vault_path(), broken);
				g_free(broken);
				vault.file_exists = FALSE;
				remmina_vault_create_new();
			}
		}
		/* with a master password the vault stays locked until remmina_vault_prompt_unlock() */
	} else {
		remmina_vault_create_new();
	}
	return TRUE;
}

static gboolean remmina_vault_plugin_is_service_available(RemminaSecretPlugin *instance)
{
	return vault.kf != NULL;
}

static const gchar *remmina_vault_profile_id(RemminaFile *remminafile)
{
	const gchar *id = remmina_file_get_filename(remminafile);

	return (id && id[0]) ? id : NULL;
}

static void remmina_vault_plugin_store_password(RemminaSecretPlugin *instance, RemminaFile *remminafile, const gchar *key, const gchar *password)
{
	TRACE_CALL(__func__);
	const gchar *id = remmina_vault_profile_id(remminafile);

	if (!id)
		return;
	remmina_vault_put(id, key, password);
}

static gchar *remmina_vault_plugin_get_password(RemminaSecretPlugin *instance, RemminaFile *remminafile, const gchar *key)
{
	TRACE_CALL(__func__);
	const gchar *id = remmina_vault_profile_id(remminafile);

	if (!vault.kf || !id)
		return NULL;
	return g_key_file_get_string(vault.kf, id, key, NULL);
}

static void remmina_vault_plugin_delete_password(RemminaSecretPlugin *instance, RemminaFile *remminafile, const gchar *key)
{
	TRACE_CALL(__func__);
	const gchar *id = remmina_vault_profile_id(remminafile);

	if (!vault.kf || !id)
		return;
	if (g_key_file_remove_key(vault.kf, id, key, NULL)) {
		gsize n = 0;
		gchar **keys = g_key_file_get_keys(vault.kf, id, &n, NULL);
		g_strfreev(keys);
		if (n == 0)
			g_key_file_remove_group(vault.kf, id, NULL);
		remmina_vault_save();
	}
}

static RemminaSecretPlugin remmina_vault_plugin = {
	REMMINA_PLUGIN_TYPE_SECRET,
	"vault",
	N_("UniDeskBrowser encrypted password vault"),
	GETTEXT_PACKAGE,
	VERSION,
	100,	/* init_order: before the keyring based plugins */
	remmina_vault_plugin_init,
	remmina_vault_plugin_is_service_available,
	remmina_vault_plugin_store_password,
	remmina_vault_plugin_get_password,
	remmina_vault_plugin_delete_password
};

void remmina_vault_plugin_register(void)
{
	TRACE_CALL(__func__);
	remmina_plugin_manager_service.register_plugin((RemminaPlugin *)&remmina_vault_plugin);
}
