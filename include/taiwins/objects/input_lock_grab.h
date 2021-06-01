/*
 * input_lock_grab.h - taiwins server input lock grab header
 *
 * Copyright (c) 2021 Xichen Zhou
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

#ifndef TW_INPUT_LOCK_GRAB_H
#define TW_INPUT_LOCK_GRAB_H


#include <wayland-server.h>

#include "surface.h"
#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_INPUT_LOCK_GRAB_ORDER	0xfffffff0

/**
 * @brief input lock grab enables a surface to lock a seat focus
 */
struct tw_input_lock_grab {
	struct tw_seat_pointer_grab pointer_grab;
	struct tw_seat_touch_grab touch_grab;
	struct tw_seat_keyboard_grab keyboard_grab;

	struct tw_seat *seat;
	struct tw_surface *locked; /**< surface which locks the input */
	struct tw_surface *ref;   /**< referenced surface */
	struct wl_signal grab_end;
	struct {
		/* lock until surface destroy */
		struct wl_listener surface_destroy;
		/* seat removed */
		struct wl_listener seat_destroy;
	} listeners;

};

void
tw_input_lock_grab_start(struct tw_input_lock_grab *grab,
                         struct tw_seat *seat, struct tw_surface *surface);
void
tw_input_lock_grab_end(struct tw_input_lock_grab *grab);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
