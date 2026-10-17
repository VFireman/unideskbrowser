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

/**
 * @file remmina_scale.c
 * @brief In a Wayland session the AppImage draws through XWayland (GDK_BACKEND=x11), where GTK takes
 * its scale from GDK_SCALE. Desktops set that variable for every program whatever the screen
 * (some export GDK_SCALE=2, meant for HiDPI): on a screen at scale 1 everything is twice as large and
 * a window holds half of what it should. So before GTK opens the display the compositor is asked
 * for its outputs and Xlib for the X screen:
 *  - the X screen as large as the outputs' pixels: X11 windows are shown pixel for pixel (Hyprland's
 *    xwayland:force_zero_scaling, Plasma's "applications scale themselves") and must scale
 *    themselves, by the outputs' scale (GTK 3 scales by whole numbers: a fractional rest goes to the
 *    text through GDK_DPI_SCALE);
 *  - smaller: the compositor scales X11 windows itself, they must not.
 * A GDK_SCALE larger than that is lowered. A smaller one, or none, is left alone: the desktop's
 * own settings (XSETTINGS, Xft.dpi) may be at work then.
 */

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "unidesk"
#endif

#include "config.h"

#include <gdk/gdk.h>
#include <stdlib.h>
#include <string.h>

#include "remmina_scale.h"

#if defined(HAVE_XWAYLAND_SCALE) && defined(GDK_WINDOWING_WAYLAND) && defined(GDK_WINDOWING_X11)
#define REMMINA_SCALE_PROBE 1
#include <X11/Xlib.h>
#include <wayland-client.h>
#ifdef HAVE_XDG_OUTPUT
#include "xdg-output-unstable-v1-client-protocol.h"
#endif
#endif

/* The desktop's values, for the programs we start */
#define ORIGINAL_SCALE		"UNIDESK_ORIGINAL_GDK_SCALE"
#define ORIGINAL_DPI_SCALE	"UNIDESK_ORIGINAL_GDK_DPI_SCALE"

#ifdef REMMINA_SCALE_PROBE

typedef struct {
	struct wl_output *	output;
#ifdef HAVE_XDG_OUTPUT
	struct zxdg_output_v1 * xdg_output;
#endif
	gint			mode_width, mode_height;	/* the current mode: the panel's pixels */
	gint			transform;
	gint			scale;				/* a whole number (rounded up) */
	gint			logical_width;			/* xdg-output: in the compositor's layout */
} ScaleOutput;

typedef struct {
	GPtrArray *			outputs;
#ifdef HAVE_XDG_OUTPUT
	struct zxdg_output_manager_v1 * xdg_output_manager;
#endif
} ScaleProbe;

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width,
			    int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform)
{
	((ScaleOutput *)data)->transform = transform;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
	ScaleOutput *out = data;

	if (flags & WL_OUTPUT_MODE_CURRENT) {
		out->mode_width = width;
		out->mode_height = height;
	}
}

static void output_done(void *data, struct wl_output *wl_output)
{
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
	((ScaleOutput *)data)->scale = factor;
}

static const struct wl_output_listener output_listener = {
	.geometry	= output_geometry,
	.mode		= output_mode,
	.done		= output_done,
	.scale		= output_scale,
};

#ifdef HAVE_XDG_OUTPUT
static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height)
{
	((ScaleOutput *)data)->logical_width = width;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void xdg_output_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
}

static void xdg_output_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position	= xdg_output_logical_position,
	.logical_size		= xdg_output_logical_size,
	.done			= xdg_output_done,
	.name			= xdg_output_name,
	.description		= xdg_output_description,
};
#endif

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	ScaleProbe *probe = data;

	if (strcmp(interface, wl_output_interface.name) == 0) {
		ScaleOutput *out = g_new0(ScaleOutput, 1);
		out->scale = 1;
		/* Version 2: the scale */
		out->output = wl_registry_bind(registry, name, &wl_output_interface, MIN(version, 2));
		wl_output_add_listener(out->output, &output_listener, out);
		g_ptr_array_add(probe->outputs, out);
	}
#ifdef HAVE_XDG_OUTPUT
	else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		probe->xdg_output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, MIN(version, 2));
	}
#endif
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global		= registry_global,
	.global_remove	= registry_global_remove,
};

static void scale_output_free(ScaleOutput *out)
{
#ifdef HAVE_XDG_OUTPUT
	if (out->xdg_output)
		zxdg_output_v1_destroy(out->xdg_output);
#endif
	wl_output_destroy(out->output);
	g_free(out);
}

/* The outputs of the compositor: the largest scale (fractional when xdg-output tells the logical
 * size) and the pixels of their modes. FALSE when there is no compositor to ask */
static gboolean scale_probe_outputs(gdouble *scale, gdouble *pixels)
{
	struct wl_display *display = wl_display_connect(NULL);
	struct wl_registry *registry;
	ScaleProbe probe = { 0 };
	guint i;

	if (!display)
		return FALSE;
	probe.outputs = g_ptr_array_new_with_free_func((GDestroyNotify)scale_output_free);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &probe);
	/* The globals, then what the outputs bound say */
	wl_display_roundtrip(display);
#ifdef HAVE_XDG_OUTPUT
	for (i = 0; probe.xdg_output_manager && i < probe.outputs->len; i++) {
		ScaleOutput *out = g_ptr_array_index(probe.outputs, i);
		out->xdg_output = zxdg_output_manager_v1_get_xdg_output(probe.xdg_output_manager, out->output);
		zxdg_output_v1_add_listener(out->xdg_output, &xdg_output_listener, out);
	}
#endif
	wl_display_roundtrip(display);

	*scale = 1;
	*pixels = 0;
	for (i = 0; i < probe.outputs->len; i++) {
		ScaleOutput *out = g_ptr_array_index(probe.outputs, i);
		/* The logical size is after rotation (odd transforms turn by 90°), the mode before */
		gint width = (out->transform & 1) ? out->mode_height : out->mode_width;
		gdouble s = out->logical_width > 0 && width > 0 ? (gdouble)width / out->logical_width : out->scale;
		*scale = MAX(*scale, s);
		*pixels += (gdouble)out->mode_width * out->mode_height;
	}
	g_ptr_array_unref(probe.outputs);
#ifdef HAVE_XDG_OUTPUT
	if (probe.xdg_output_manager)
		zxdg_output_manager_v1_destroy(probe.xdg_output_manager);
#endif
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return *pixels > 0;
}

/* The pixels of the X screen (XWayland): the outputs side by side */
static gboolean scale_probe_x11(gdouble *pixels)
{
	Display *display = XOpenDisplay(NULL);

	if (!display)
		return FALSE;
	*pixels = (gdouble)DisplayWidth(display, DefaultScreen(display)) * DisplayHeight(display, DefaultScreen(display));
	XCloseDisplay(display);
	return *pixels > 0;
}

#endif /* REMMINA_SCALE_PROBE */

void remmina_scale_adapt(void)
{
#ifdef REMMINA_SCALE_PROBE
	const gchar *backend = g_getenv("GDK_BACKEND");
	const gchar *env_scale = g_getenv("GDK_SCALE");
	const gchar *env_dpi = g_getenv("GDK_DPI_SCALE");
	gdouble screen_scale, output_pixels, x11_pixels, dpi, text;
	gboolean pixel_for_pixel;
	gint scale, need;
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE], *value;

	/* Only GTK through XWayland: natively on Wayland the compositor gives the scale itself */
	if (!g_getenv("WAYLAND_DISPLAY") || !backend || !g_str_has_prefix(backend, "x11") || !env_scale)
		return;
	scale = atoi(env_scale);
	if (scale <= 1 || !scale_probe_outputs(&screen_scale, &output_pixels) || !scale_probe_x11(&x11_pixels))
		return;
	/* Pixel for pixel, the X screen spans the outputs' pixels; scaled by the compositor it is a
	 * scale² smaller */
	pixel_for_pixel = x11_pixels >= output_pixels * 0.9;
	if (!pixel_for_pixel)
		need = 1;
	else if (screen_scale < 1.75)
		need = 1;
	else
		need = (gint)(screen_scale + 0.25);
	if (scale <= need)
		return;

	/* The text keeps its share of the desktop's setting, and the fractional rest of the scale */
	dpi = env_dpi ? g_ascii_strtod(env_dpi, NULL) : 1;
	if (dpi <= 0)
		dpi = 1;
	text = dpi * (pixel_for_pixel ? screen_scale / need : 1);
	g_setenv(ORIGINAL_SCALE, env_scale, TRUE);
	g_setenv(ORIGINAL_DPI_SCALE, env_dpi ? env_dpi : "", TRUE);
	value = g_strdup_printf("%d", need);
	g_setenv("GDK_SCALE", value, TRUE);
	g_free(value);
	if (env_dpi || ABS(text - 1) > 0.01)
		g_setenv("GDK_DPI_SCALE", g_ascii_formatd(buf, sizeof(buf), "%.3f", text), TRUE);
	g_message("Screen scale %.2f, X11 windows %s: GDK_SCALE=%d from the environment is lowered to %d%s%s",
		  screen_scale, pixel_for_pixel ? "shown pixel for pixel" : "scaled by the compositor", scale, need,
		  g_getenv("GDK_DPI_SCALE") ? ", GDK_DPI_SCALE=" : "", g_getenv("GDK_DPI_SCALE") ? g_getenv("GDK_DPI_SCALE") : "");
#endif
}

void remmina_scale_launch_context(GAppLaunchContext *ctx)
{
	const gchar *scale = g_getenv(ORIGINAL_SCALE);
	const gchar *dpi = g_getenv(ORIGINAL_DPI_SCALE);

	if (!scale)
		return;
	g_app_launch_context_setenv(ctx, "GDK_SCALE", scale);
	if (dpi && dpi[0])
		g_app_launch_context_setenv(ctx, "GDK_DPI_SCALE", dpi);
	else
		g_app_launch_context_unsetenv(ctx, "GDK_DPI_SCALE");
	g_app_launch_context_unsetenv(ctx, ORIGINAL_SCALE);
	g_app_launch_context_unsetenv(ctx, ORIGINAL_DPI_SCALE);
}
