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
#include <string.h>
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
#include <taiwins/objects/egl.h>
#include <taiwins/backend.h>
#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include "internal.h"

/******************************************************************************
 * XCB event handling
 *****************************************************************************/

static void
handle_x11_configure_notify(struct tw_output_device *device,
                            xcb_configure_notify_event_t *ev)
{
	int refresh = device->current.current_mode.refresh;
	if (ev->width > 0 && ev->height > 0)
		tw_output_device_set_current_mode(device, ev->width, ev->height,
		                                  refresh);
}

static void
handle_new_x11_frame(void *data)
{
	struct tw_x11_output *output = data;

	wl_signal_emit(&output->output.device.signals.new_frame,
	               &output->output.device);
}

static void
handle_x11_request_frame(struct tw_x11_backend *x11,
                         struct tw_x11_output *output)
{
	struct wl_display *display = x11->display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	if (!loop)
	        return;
        wl_event_loop_add_idle(loop, handle_new_x11_frame, output);
}

static int
x11_handle_events(int fd, uint32_t mask, void *data)
{
	struct tw_x11_backend *x11 = data;
	xcb_generic_event_t *e;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl_display_terminate(x11->display);
		return 0;
	}
	while ((e = xcb_poll_for_event(x11->xcb_conn))) {
		switch (e->response_type & 0x7f) {
		case XCB_EXPOSE: {
			xcb_expose_event_t *ev = (xcb_expose_event_t *)e;
			struct tw_x11_output *output =
				tw_x11_output_from_id(x11, ev->window);
			if (output)
				handle_x11_request_frame(x11, output);
			break;
		}
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *ev =
				(xcb_configure_notify_event_t *)e;
			struct tw_x11_output *output =
				tw_x11_output_from_id(x11, ev->window);

			if (output)
				handle_x11_configure_notify(
					&output->output.device, ev);
			break;
		}
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *ev =
				(xcb_client_message_event_t *)e;
			if (ev->data.data32[0]==x11->atoms.wm_delete_window) {
				struct tw_x11_output *output =
					tw_x11_output_from_id(x11, ev->window);
				if (output != NULL)
					tw_x11_remove_output(output);
			}
			break;
		}
		case XCB_GE_GENERIC: {
			xcb_ge_generic_event_t *ev =
				(xcb_ge_generic_event_t *)e;
			if (ev->extension == x11->xinput_opcode)
				tw_x11_handle_input_event(x11, ev);
			break;
		}

		}
		free(e);
	}

	return 0;

}

/******************************************************************************
 * backend implemenentation
 *****************************************************************************/

static const struct tw_egl_options *
x11_gen_egl_params(struct tw_backend *backend)
{
	static struct tw_egl_options egl_opts = {0};
	struct tw_x11_backend *x11 = wl_container_of(backend, x11, base);

	egl_opts.platform = EGL_PLATFORM_X11_KHR;
	egl_opts.native_display = x11->x11_dpy;
	return &egl_opts;
}

static bool
x11_start_backend(struct tw_backend *backend,
                  struct tw_render_context *ctx)
{
	struct tw_x11_output *output;
	struct tw_x11_backend *x11 = wl_container_of(backend, x11, base);

	if (wl_list_length(&x11->base.outputs) == 0)
		tw_x11_backend_add_output(&x11->base, 1000, 720);

	wl_list_for_each(output, &x11->base.outputs, output.device.link)
		tw_x11_output_start_maybe(output);
	wl_signal_emit(&x11->base.signals.new_input, &x11->keyboard);

	return true;
}

static const struct tw_backend_impl x11_impl = {
	.start = x11_start_backend,
	.gen_egl_params = x11_gen_egl_params,
};

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
x11_check_xfixes(struct tw_x11_backend *x11)
{
	const xcb_query_extension_reply_t *ext;

	ext = xcb_get_extension_data(x11->xcb_conn, &xcb_xfixes_id);
	if (!ext || !ext->present)
		return false;
	xcb_xfixes_query_version_cookie_t fixes_cookie =
		xcb_xfixes_query_version(x11->xcb_conn, 4, 0);
	xcb_xfixes_query_version_reply_t *fixes_reply =
		xcb_xfixes_query_version_reply(x11->xcb_conn, fixes_cookie,
		                               NULL);

	if (!fixes_reply || fixes_reply->major_version < 4) {
		free(fixes_reply);
		return false;
	}
	free(fixes_reply);
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
x11_backend_stop(struct tw_x11_backend *x11)
{
	struct tw_x11_output *output, *tmp;

	if (!x11->base.ctx)
		return;

        wl_signal_emit(&x11->base.signals.stop, &x11->base);
        wl_list_for_each_safe(output, tmp, &x11->base.outputs,
                              output.device.link)
		tw_x11_remove_output(output);
	tw_input_device_fini(&x11->keyboard);

	wl_list_remove(&x11->base.render_context_destroy.link);
	x11->base.ctx = NULL;
}

static void
notify_x11_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_x11_backend *x11 =
		wl_container_of(listener, x11, display_destroy);

	wl_list_remove(&listener->link);
	if (x11->event_source)
		wl_event_source_remove(x11->event_source);
	//destroy the render context now as it needs x11 connection for fini.
	if (x11->base.ctx)
		tw_render_context_destroy(x11->base.ctx);

	XCloseDisplay(x11->x11_dpy);
	free(x11);
}

static void
notify_x11_render_context_destroy(struct wl_listener *listener, void *data)
{
	struct tw_x11_backend *x11 =
		wl_container_of(listener, x11, base.render_context_destroy);

        x11_backend_stop(x11);
}

WL_EXPORT struct tw_backend *
tw_x11_backend_create(struct wl_display *display, const char *x11_display)
{
	struct tw_x11_backend *x11 = calloc(1, sizeof(*x11));

	if (!x11)
		return NULL;
	x11->display = display;

	tw_backend_init(&x11->base);
	x11->base.impl = &x11_impl;
	x11->base.render_context_destroy.notify =
		notify_x11_render_context_destroy;

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

	if (!x11_check_xfixes(x11)) {
		tw_logl_level(TW_LOG_ERRO, "XCB xfixes ext not supported");
		goto err_dpy;
	}

	if (!x11_check_xinput(x11)) {
		tw_logl_level(TW_LOG_ERRO, "XCB xinput ext not supported");
		goto err_dpy;
	}
	if (!x11_set_event_source(x11, display)) {
		tw_logl_level(TW_LOG_ERRO, "failed to set wl_event_source "
		              "for x11 backend");
		goto err_dpy;
	}

	tw_input_device_init(&x11->keyboard, TW_INPUT_TYPE_KEYBOARD, 0, NULL);
	strncpy(x11->keyboard.name, "X11-keyboard",
	        sizeof(x11->keyboard.name));
	wl_list_insert(x11->base.inputs.prev, &x11->keyboard.link);

	x11->display = display;
	tw_set_display_destroy_listener(x11->display, &x11->display_destroy,
	                                notify_x11_display_destroy);

	return &x11->base;
err_dpy:
	XCloseDisplay(x11->x11_dpy);
err_conn:
	free(x11);
	return NULL;
}

WL_EXPORT bool
tw_backend_is_x11(struct tw_backend *backend)
{
	return backend->impl == &x11_impl;
}
