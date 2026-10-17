/*
 * Unidesk - sessions: the tabs of a window, kept when it closes and at quit
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

/**
 * @file remmina_session.h
 * @brief A session is what one window holds: its tabs (connections) and its place on the screen.
 *
 * Every application window has a session. It is written to $XDG_CONFIG_HOME/unidesk/sessions.db
 * (a key file with a format version, written atomically, mode 0600) while the window lives, so
 * after a crash too the last state is there. A window closed while others stay open leaves a
 * "recently closed" session; at quit the open sessions stay "open": they are the ones "Continue"
 * restores at the next start. A session the user named (or saved) is kept until deleted; of the
 * others only the newest closed ones are kept. Passwords are never written here.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	REMMINA_SESSION_OPEN,	/* a window shows it, or showed it when the application quit */
	REMMINA_SESSION_CLOSED,	/* its window was closed: recently closed, or saved */
} RemminaSessionState;

/* One tab: a saved profile, or what a quick connection is rebuilt from (no password) */
typedef struct {
	gchar *	profile;	/* "db:…" profile id; NULL for a quick connection */
	gchar *	name;		/* tab title */
	gchar *	protocol;	/* quick connection */
	gchar *	server;
	gchar *	username;
	gchar *	sftp_left;	/* SFTP tab: the folders of its panels */
	gchar *	sftp_right;
} RemminaSessionTab;

typedef struct {
	gchar *			id;		/* "s" + 16 hex digits */
	gchar *			name;		/* "Session 3" until the user names it */
	gboolean		saved;		/* named or saved by the user: kept until deleted */
	RemminaSessionState	state;
	gint64			updated_at;	/* microseconds since the epoch (g_get_real_time) */
	gint64			closed_at;
	gint			x, y, width, height;
	gboolean		maximized;
	gint			active;		/* index of the current tab in tabs, -1: the home tab */
	GPtrArray *		tabs;		/* RemminaSessionTab */
	gchar *			color;		/* the window accent ("green"…), NULL: none */
} RemminaSession;

/* How many unnamed closed sessions are kept */
#define REMMINA_SESSION_KEEP_CLOSED 10

RemminaSessionTab *remmina_session_tab_new(void);
void remmina_session_tab_free(RemminaSessionTab *tab);

/* A new session for a new window: fresh id, the next "Session N" name, open, no tabs */
RemminaSession *remmina_session_new(void);
/* The colour of the next new window, step by step round the palette: 0, 1, 2… (kept in the file) */
gint remmina_session_next_color(void);
RemminaSession *remmina_session_copy(const RemminaSession *session);
void remmina_session_free(RemminaSession *session);

/* Writes the session (new or replacing the one with its id) and saves the file */
void remmina_session_store(const RemminaSession *session);
/* A copy, NULL when there is none */
RemminaSession *remmina_session_lookup(const gchar *id);
/* Copies of all sessions: open ones first, then by last change, newest first */
GPtrArray *remmina_session_list(void);
void remmina_session_delete(const gchar *id);
/* A name from the user: the session is kept until deleted */
void remmina_session_rename(const gchar *id, const gchar *name);
void remmina_session_set_saved(const gchar *id, gboolean saved);
/* Its window closed while others stay open: recently closed (dropped if it has no tab and no name) */
void remmina_session_close(const gchar *id);

/* At start: the sessions that were open at the last quit (or crash), for "Continue". They are
 * marked closed in the file at once, so they stay among the recently closed if not continued. */
GPtrArray *remmina_session_take_continue_set(void);

G_END_DECLS
