/*
 * internal.h - taiwins server wayland backend internal header
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

#ifndef TW_WL_INTERNAL_H
#define TW_WL_INTERNAL_H

#include <wayland-egl.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-xdg-shell-client-protocol.h>
#include <taiwins/input_device.h>
#include <taiwins/render_output.h>

#include <taiwins/backend.h>


#ifdef  __cplusplus
extern "C" {
#endif

struct tw_wl_seat {
	struct wl_seat *wl_seat;
	struct tw_wl_backend *wl;
	struct wl_list link;
	uint32_t caps, name; /* seat0, seat1, etc */
	uint32_t gid; /* wl_object id */

	struct tw_input_device keyboard_dev, pointer_dev;
	struct wl_pointer *wl_pointer;
	struct wl_keyboard *wl_keyboard;
};

struct tw_wl_output {
	struct tw_render_output output;

        struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_egl_window *egl_window;
	struct tw_wl_backend *wl;

        struct wl_pointer *curr_pointer;
};

struct tw_wl_backend {
	struct wl_display *server_display;
	struct wl_display *remote_display; /* the display we connect to */
	struct wl_registry *registry;
	struct wl_event_source *event_src;

	struct {
		struct wl_compositor *compositor;
		struct xdg_wm_base *wm_base;
	} globals;

	struct tw_backend base;
	struct wl_listener display_destroy;
	/** here wl_output is represented with wl_surface */
	struct wl_list seats;
};

struct tw_wl_seat *
tw_wl_handle_new_seat(struct tw_wl_backend *wl, struct wl_registry *reg,
                      uint32_t id, uint32_t version);
void
tw_wl_seat_start(struct tw_wl_seat *seat);

void
tw_wl_remove_output(struct tw_wl_output *output);

void
tw_wl_output_start(struct tw_wl_output *output);

void
tw_wl_bind_wl_registry(struct tw_wl_backend *wl);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
