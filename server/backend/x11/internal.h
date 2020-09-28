/*
 * internal.c - taiwins server x11 internal header
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

#ifndef TW_X11_INTERNAL_H
#define TW_X11_INTERNAL_H

#include <wayland-server.h>

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xinput.h>
#include <xcb/xproto.h>

#include "backend.h"
#include "input_device.h"
#include "output_device.h"
#include "render_context.h"

#ifdef  __cplusplus
extern "C" {
#endif

//Here we allow multiple displays?
struct tw_x11_backend {
	struct wl_display *display;
	struct wl_event_source *event_source; /**< for x11 events */
	struct tw_backend *base;
	struct tw_render_context *ctx;
	struct tw_backend_impl impl;
	struct wl_listener display_destroy;
	/* allow multiple outputs */
	struct wl_list outputs;

	Display *x11_dpy;
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	uint8_t xinput_opcode;

	struct {
		xcb_atom_t wm_protocols;
		xcb_atom_t wm_normal_hint;
		xcb_atom_t wm_size_hint;
		xcb_atom_t wm_delete_window;
		xcb_atom_t wm_class;
		xcb_atom_t net_wm_name;
		xcb_atom_t utf8_string;
		xcb_atom_t variable_refresh;
	} atoms;

	struct {
		struct tw_input_device device;
	} keyboard;
};

struct tw_x11_output {
	struct tw_output_device device;
	struct tw_x11_backend *x11;
	struct wl_event_source *frame_timer;
        unsigned int width, height;

	xcb_window_t win;
	struct tw_render_surface render_surface;
	struct wl_listener info_listener;
	unsigned int frame_interval;
};

int
x11_handle_events(int fd, uint32_t mask, void *data);

bool
tw_x11_output_start(struct tw_x11_output *output);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
