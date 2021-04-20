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

#include <time.h>
#include <wayland-egl.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-xdg-shell-client-protocol.h>
#include <wayland-presentation-time-client-protocol.h>
#include <taiwins/input_device.h>
#include <taiwins/render_output.h>
#include <taiwins/backend_wayland.h>

#include "render.h"
#include "output_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_wl_output {
	struct wl_output *wl_output;
	struct tw_wl_backend *wl;
	struct wl_list link; /* tw_wl_backend:outputs */

	uint32_t w,h,r;
	int32_t scale;
};

struct tw_wl_surface {
	struct tw_render_output output;
	struct wl_listener output_commit;

        struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_egl_window *egl_window;
	struct wl_callback *frame;
	struct tw_wl_backend *wl;
	struct tw_wl_output *residing;
};

struct tw_wl_seat {
	struct wl_seat *wl_seat;
	struct tw_wl_backend *wl;
	struct wl_list link; /* tw_wl_backend:seats */
	uint32_t caps, name; /* seat0, seat1, etc */
	uint32_t gid; /* wl_object id */

	struct tw_input_device keyboard_dev, pointer_dev;
	struct wl_pointer *wl_pointer;
	struct wl_keyboard *wl_keyboard;
	struct tw_wl_surface *pointer_focus;
};


struct tw_wl_backend {
	struct wl_display *server_display;
	struct wl_display *remote_display; /* the display we connect to */
	struct wl_registry *registry;
	struct wl_event_source *event_src;

	struct {
		struct wl_compositor *compositor;
		struct xdg_wm_base *wm_base;
		struct wp_presentation *presentation;
	} globals;

	struct tw_backend base;
	struct wl_listener display_destroy;

	struct wl_list seats; /**< tw_wl_seat:link */
	struct wl_list outputs; /**< tw_wl_output:link */

	clockid_t clk_id;
};

struct tw_wl_seat *
tw_wl_handle_new_seat(struct tw_wl_backend *wl, struct wl_registry *reg,
                      uint32_t id, uint32_t version);
struct tw_wl_output *
tw_wl_handle_new_output(struct tw_wl_backend *wl, struct wl_registry *reg,
                        uint32_t id, uint32_t version);
void
tw_wl_output_remove(struct tw_wl_output *output);

void
tw_wl_seat_start(struct tw_wl_seat *seat);

void
tw_wl_seat_remove(struct tw_wl_seat *seat);

void
tw_wl_surface_remove(struct tw_wl_surface *output);

void
tw_wl_surface_start_maybe(struct tw_wl_surface *output);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
