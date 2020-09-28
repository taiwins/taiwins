/*
 * backend.c - taiwins server x11 backend implementation
 *
 * Copyright (c) 2020 Xichen Zhou
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server.h>

#include <wayland-util.h>
#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xinput.h>
#include <xcb/xproto.h>

#include <ctypes/helpers.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include "backend.h"
#include "input_device.h"
#include "render_context.h"
#include "egl.h"
#include "internal.h"


/******************************************************************************
 * backend implemenentation
 *****************************************************************************/

static const struct tw_egl_options *
x11_gen_egl_params(struct tw_backend *backend)
{
	static struct tw_egl_options egl_opts = {0};
	struct tw_x11_backend *x11 =
		wl_container_of(backend->impl, x11, impl);

	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};

	egl_opts.platform = EGL_PLATFORM_X11_KHR;
	egl_opts.native_display = x11->x11_dpy;
	egl_opts.visual_id = x11->screen->root_visual;
	egl_opts.context_attribs = (EGLint *)egl_config_attribs;

	return &egl_opts;
}

static bool
x11_start_backend(struct tw_backend *backend,
                  struct tw_render_context *ctx)
{
	struct tw_x11_output *output;

	struct tw_x11_backend *x11 =
		wl_container_of(backend->impl, x11, impl);
	x11->ctx = ctx;

	wl_list_for_each(output, &x11->outputs, device.link)
		tw_x11_output_start(output);

	return true;
}

/******************************************************************************
 * initializer
 *****************************************************************************/

static xcb_screen_t *
screen_of_display(xcb_connection_t *conn, int screen)
{
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (int i = 0; iter.rem; xcb_screen_next(&iter), i++)
		if (i == screen)
			return iter.data;
	return NULL;
}

static void
x11_get_atoms(struct tw_x11_backend *x11)
{
	struct {
		const char *name;
		xcb_intern_atom_cookie_t cookie;
		xcb_atom_t *atom;
	} atoms[] = {
		{.name = "WM_PROTOCOLS", .atom = &x11->atoms.wm_protocols },
		{.name = "WM_NORMAL_HINTS", .atom =
		 &x11->atoms.wm_normal_hint },
		{.name = "WM_SIZE_HINT", .atom = &x11->atoms.wm_size_hint },
		{.name = "WM_DELETE_WINDOW",
		 .atom = &x11->atoms.wm_delete_window},
		{.name = "WM_CLASS", .atom = &x11->atoms.wm_class },
	};
	//request and reply
	for (unsigned i = 0; i < NUMOF(atoms); i++)
		atoms[i].cookie = xcb_intern_atom(x11->xcb_conn, true,
		                                  strlen(atoms[i].name),
		                                  atoms[i].name);
	for (unsigned i = 0; i < NUMOF(atoms); i++) {
		xcb_intern_atom_reply_t *reply =
			xcb_intern_atom_reply(x11->xcb_conn, atoms[i].cookie,
			                      NULL);
		if (reply) {
			*atoms[i].atom = reply->atom;
			free(reply);
		} else {
			*atoms[i].atom = XCB_ATOM_NONE;
		}
	}

}

static bool
x11_check_xinput(struct tw_x11_backend *x11)
{
	uint8_t xinput_opcode;
	const xcb_query_extension_reply_t *ext;
	xcb_input_xi_query_version_cookie_t cookie;
	xcb_input_xi_query_version_reply_t *reply;

	ext = xcb_get_extension_data(x11->xcb_conn, &xcb_input_id);
	if (!ext || !ext->present)
		return false;
	xinput_opcode = ext->major_opcode;
	cookie = xcb_input_xi_query_version(x11->xcb_conn, 2, 0);
	reply = xcb_input_xi_query_version_reply(x11->xcb_conn, cookie,
	                                            NULL);
	if (!reply || reply->major_version < 2) {
		tw_logl_level(TW_LOG_WARN, "xinput version 2 not supported");
		free(reply);
		return false;
	}
	x11->xinput_opcode = xinput_opcode;
	free(reply);
	return true;
}

static bool
x11_set_event_source(struct tw_x11_backend *x11, struct wl_display *display)
{
	int fd = xcb_get_file_descriptor(x11->xcb_conn);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	uint32_t events = WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR;
	x11->event_source = wl_event_loop_add_fd(loop, fd, events,
	                                         x11_handle_events, x11);
	if (!x11->event_source)
		return false;
	return true;
}

static void
x11_backend_destroy(struct tw_x11_backend *x11)
{
	wl_signal_emit(&x11->impl.events.destroy, &x11->impl);

	if (x11->event_source)
		wl_event_source_remove(x11->event_source);
	wl_list_remove(&x11->display_destroy.link);
	//you see, here is the problem, if we attach the render context on
	//starting the backend now we don't know when to destroy it.

	XCloseDisplay(x11->x11_dpy);
	free(x11);
}

static void
notify_x11_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_x11_backend *x11 =
		wl_container_of(listener, x11, display_destroy);
	x11_backend_destroy(x11);
}

struct tw_backend_impl *
tw_x11_backend_create(struct wl_display *display, const char *x11_display)
{
	struct tw_x11_backend *x11 = calloc(1, sizeof(*x11));

	if (!x11)
		return NULL;
	x11->display = display;
	wl_list_init(&x11->outputs);
	wl_signal_init(&x11->impl.events.destroy);
	wl_signal_init(&x11->impl.events.new_input);
	wl_signal_init(&x11->impl.events.new_output);
	x11->impl.gen_egl_params = x11_gen_egl_params;
	x11->impl.start = x11_start_backend;
	//EGL supports only x11 connection, so we will use X11 connection later
	//if we implement vulkan, we will have to use x11 connection as well.
	x11->x11_dpy = XOpenDisplay(x11_display);
	if (!x11->x11_dpy) {
		tw_logl_level(TW_LOG_ERRO, "Failed to open X11 connection");
		goto err_conn;
	}
	x11->xcb_conn = XGetXCBConnection(x11->x11_dpy);
	if (!x11->xcb_conn || xcb_connection_has_error(x11->xcb_conn)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to open xcb connection");
		goto err_dpy;
	}
	XSetEventQueueOwner(x11->x11_dpy, XCBOwnsEventQueue);
	x11->screen = screen_of_display(x11->xcb_conn,
	                                XDefaultScreen(x11->x11_dpy));
	if (!x11->screen)
		goto err_dpy;
	x11_get_atoms(x11);

	if (!x11_check_xinput(x11)) {
		tw_logl_level(TW_LOG_ERRO, "xinput not supported");
		goto err_dpy;
	}
	if (!x11_set_event_source(x11, display)) {
		tw_logl_level(TW_LOG_ERRO, "failed to set wl_event_source "
		              "for x11 backend");
		goto err_dpy;
	}

	x11->display = display;
	tw_set_display_destroy_listener(x11->display, &x11->display_destroy,
	                                notify_x11_display_destroy);

	return &x11->impl;
err_dpy:
	XCloseDisplay(x11->x11_dpy);
err_conn:
	free(x11);
	return NULL;
}
