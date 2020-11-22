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

#include <taiwins/backend_x11.h>
#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define _XINPUT_EVENT_MASK (XCB_INPUT_XI_EVENT_MASK_KEY_PRESS |         \
                            XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE |       \
                            XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |      \
                            XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |    \
                            XCB_INPUT_XI_EVENT_MASK_MOTION |            \
                            XCB_INPUT_XI_EVENT_MASK_ENTER |             \
                            XCB_INPUT_XI_EVENT_MASK_LEAVE)

//Here we allow multiple displays?
struct tw_x11_backend {
	struct wl_display *display;
	struct wl_event_source *event_source; /**< for x11 events */
	struct tw_backend base;
	struct wl_listener display_destroy;

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

	struct tw_input_device keyboard;
};

struct tw_x11_output {
	struct tw_render_output output;
	struct tw_x11_backend *x11;
	struct wl_event_source *frame_timer;

	xcb_window_t win;
	struct wl_listener output_commit_listener;
	unsigned int frame_interval;

	struct tw_input_device pointer;
};

void
tw_x11_handle_input_event(struct tw_x11_backend *x11,
                          xcb_ge_generic_event_t *ge);
bool
tw_x11_output_start(struct tw_x11_output *output);

void
tw_x11_remove_output(struct tw_x11_output *output);

static inline struct tw_x11_output *
tw_x11_output_from_id(struct tw_x11_backend *x11, xcb_window_t id)
{
	struct tw_x11_output *output;

	wl_list_for_each(output, &x11->base.outputs, output.device.link) {
		if (output->win == id)
			return output;
	}
	return NULL;
}

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
