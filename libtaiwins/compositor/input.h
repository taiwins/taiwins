/*
 * input.h - taiwins server input headers
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

#ifndef TW_SEAT_INPUT_H
#define TW_SEAT_INPUT_H

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <taiwins/objects/seat.h>

#include "engine.h"
#include "bindings.h"
#include "input_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_seat_events {
	struct tw_seat *seat;
	struct tw_engine *engine;
	struct tw_bindings *bindings;
	struct xkb_state *curr_state;

	struct wl_listener key_input;
	struct wl_listener btn_input;
	struct wl_listener axis_input;
	struct wl_listener tch_input;

	struct tw_seat_keyboard_grab session_switch_grab;
	struct tw_seat_keyboard_grab binding_key_grab;
	struct tw_seat_pointer_grab binding_pointer_grab;
	struct tw_seat_touch_grab binding_touch_grab;
};

void
tw_seat_events_init(struct tw_seat_events *events,
                    struct tw_engine_seat *seat,
                    struct tw_bindings *bindings);
void
tw_seat_events_fini(struct tw_seat_events *events);

void
tw_bindings_add_dummy(struct tw_bindings *bindings);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
