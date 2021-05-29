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
#include <taiwins/objects/virtual_keyboard.h>
#include <taiwins/objects/input_method.h>
#include <taiwins/objects/text_input.h>
#include <taiwins/engine.h>
#include <taiwins/input_device.h>

#include "bindings.h"
#include "config/config.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_seat_listeners {
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

struct tw_server_input_manager {
	struct tw_engine *engine;
	struct tw_config *config;
	struct tw_seat_listeners inputs[8];
	//globals
	struct tw_virtual_keyboard_manager vkeyboard_mgr;
	struct tw_input_method_manager im_mgr;
	struct tw_text_input_manager ti_mgr;

	struct {
		struct wl_listener seat_add;
		struct wl_listener seat_remove;
		struct wl_listener display_destroy;
		struct wl_listener new_virtual_keyboard;
	} listeners;
};

struct tw_server_input_manager *
tw_server_input_manager_create_global(struct tw_engine *engine,
                                      struct tw_config *config);
void
tw_bindings_add_dummy(struct tw_bindings *bindings);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
