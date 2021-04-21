/*
 * xdg_grab.h - taiwins desktop grab header
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

#ifndef TW_XDG_GRAB_H
#define TW_XDG_GRAB_H

#include <pixman.h>
#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#include <taiwins/objects/seat.h>
#include <taiwins/objects/desktop.h>

#include "xdg.h"
#include "layout.h"
#include "workspace.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xdg_grab_interface {
	union {
		struct tw_seat_pointer_grab pointer_grab;
		struct tw_seat_touch_grab touch_grab;
		struct tw_seat_keyboard_grab keyboard_grab;
	};
	/* need this struct to access the workspace */
	struct wl_listener view_destroy_listener;
	struct tw_xdg_view *view;
	struct tw_xdg *xdg;
	float gx, gy, dx, dy;
	enum wl_shell_surface_resize edge;
	uint32_t mod_mask;
	struct wl_event_source *idle_motion_source;
};


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
