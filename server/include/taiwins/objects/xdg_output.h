/*
 * xdg_output.h - taiwins xdg-output protocol header
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

#ifndef TW_XDG_OUTPUT_H
#define TW_XDG_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xdg_output_manager {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_listener display_destroy_listener;
	struct wl_signal xdg_output_requested;
};

struct tw_xdg_output_info_event {
	struct wl_resource *wl_output;
	const char *name, *desription;
	int32_t x, y;
	uint32_t width, height;
};

bool
tw_xdg_output_manager_init(struct tw_xdg_output_manager *manager,
                           struct wl_display *display);
struct tw_xdg_output_manager *
tw_xdg_output_manager_create_global(struct wl_display *display);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
