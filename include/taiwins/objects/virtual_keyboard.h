/*
 * virtual_keyboard.h - taiwins zwp_virtual_keyboard header
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

#ifndef TW_VIRTUAL_KEYBOARD_H
#define TW_VIRTUAL_KEYBOARD_H

#include <wayland-server-core.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_virtual_keyboard_manager {
	struct wl_global *global;
	struct wl_signal new_keyboard;

	struct wl_listener display_destroy;
};

struct tw_virtual_keyboard {
	struct tw_seat *seat;
	struct wl_resource *resource;
	struct wl_signal destroy_signal;

        struct {
		struct wl_listener mgr_destroy;
		struct wl_listener seat_destroy;
	} listeners;
};

bool
tw_virtual_keyboard_manager_init(struct tw_virtual_keyboard_manager *mgr,
                                 struct wl_display *display);
struct tw_virtual_keyboard_manager *
tw_virtual_keyboard_manager_create_global(struct wl_display *display);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
