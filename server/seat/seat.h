/*
 * input.h - taiwins server backend input header
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

#ifndef TW_SEAT_H
#define TW_SEAT_H

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#ifdef  __cplusplus
extern "C" {
#endif


/* If here we implement a front end for a seat, similar to what wlr_seat has,
 * which does not take any basic backend specific code, maybe we can integrate
 * this into taiwins.
 *
 * A binding system would implement a grab. Just like normal grab. This is
 * similar in wlroots and libweston.
 *
 * Otherwise, how do you start a new grab, this is clearly a issue. We do not
 * want binding to know about backend and vice versa so a glue is needed in the
 * compositor.
 */

struct tw_seat; //ALL shall we call it twp_seat
struct tw_seat_client;

struct tw_seat_pointer_grab;

struct tw_pointer_grab_interface {
	void (*enter)(struct tw_seat_pointer_grab *grab,
	              struct wl_resource *surface, double sx, double sy);
	void (*motion)(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
	               double sx, double sy);
	uint32_t (*button)(struct tw_seat_pointer_grab *grab,
	                   uint32_t time_msec, uint32_t button,
	                   enum wl_pointer_button_state state);
	void (*axis)(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
	             enum wl_pointer_axis orientation, double value,
	             int32_t value_discrete,
	             enum wl_pointer_axis_source source);
	void (*frame)(struct tw_seat_pointer_grab *grab);
	void (*cancel)(struct tw_seat_pointer_grab *grab);
};

struct tw_seat_keyboard_grab;

struct tw_keyboard_grab_interface {
	void (*enter)(struct tw_seat_keyboard_grab *grab,
	              struct wl_resource *surface, uint32_t keycodes[],
	              size_t n_keycodes);
	void (*key)(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
	            uint32_t key, uint32_t state);
	void (*modifiers)(struct tw_seat_keyboard_grab *grab,
	                  //we can do the weston way,
	                  uint32_t mods_depressed, uint32_t mods_latched,
	                  uint32_t mods_locked, uint32_t group);
	void (*cancel)(struct tw_seat_keyboard_grab *grab);
};

struct tw_seat_touch_grab;

struct tw_touch_grab_interface {
	uint32_t (*down)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	                 uint32_t touch_id, wl_fixed_t sx, wl_fixed_t sy);
	void (*up)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	           uint32_t touch_id);
	void (*motion)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	               uint32_t touch_id, wl_fixed_t sx, wl_fixed_t sy);
	void (*enter)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	              struct wl_resource *surface, uint32_t touch_id,
	              wl_fixed_t sx, wl_fixed_t sy);
	void (*touch_cancel)(struct tw_seat_touch_grab *grab);
	void (*cancel)(struct tw_seat_touch_grab *grab);
};

struct tw_seat_touch_grab {
	const struct tw_touch_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
};

struct tw_seat_keyboard_grab {
	const struct tw_keyboard_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
};

struct tw_seat_pointer_grab {
	const struct tw_pointer_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
};

struct tw_pointer_new_cursor_event {
	struct wl_resource *surface;
	uint32_t hotspot_x;
	uint32_t hotspot_y;
};

struct tw_keyboard {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;
	struct xkb_keymap *keymap;
	size_t keymap_size;
	char *keymap_string;

	struct wl_listener event;
	struct tw_seat_keyboard_grab default_grab;
	struct tw_seat_keyboard_grab *grab;
};

struct tw_pointer {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;

	struct wl_listener event;
	struct tw_seat_pointer_grab default_grab;
	struct tw_seat_pointer_grab *grab;
};

struct tw_touch {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;

	struct wl_listener event;
	struct tw_seat_touch_grab default_grab;
	struct tw_seat_touch_grab *grab;
};

struct tw_seat {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list clients;
	struct wl_list link;
	char name[32];

	uint32_t capabilities;
	uint32_t repeat_delay, repeat_rate;
	struct tw_keyboard keyboard;
	struct tw_pointer pointer;
	struct tw_touch touch;

	struct wl_signal new_cursor_signal;
};

struct tw_seat_client {
	struct tw_seat *seat;
	struct wl_resource *resource;
	struct wl_client *client;
	struct wl_list link;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touches;
};

//seat API, implement wl_seat on the server side.
struct tw_seat *
tw_seat_create(struct wl_display *display, const char *name);

void
tw_seat_destroy(struct tw_seat *seat);

void
tw_seat_set_name(struct tw_seat *seat, const char *name);

void
tw_seat_set_key_repeat_rate(struct tw_seat *seat, uint32_t delay,
                            uint32_t rate);
void
tw_seat_send_capabilities(struct tw_seat *seat);

struct tw_keyboard *
tw_seat_new_keyboard(struct tw_seat *seat);

void
tw_seat_remove_keyboard(struct tw_seat *seat);

void
tw_keyboard_start_grab(struct tw_keyboard *keyboard,
                       struct tw_seat_keyboard_grab *grab);
void
tw_keyboard_end_grab(struct tw_keyboard *keyboard);

void
tw_keyboard_set_keymap(struct tw_keyboard *keyboard,
                       struct xkb_keymap *keymap);
void
tw_keyboard_send_keymap(struct tw_keyboard *keyboard,
                        struct wl_resource *keyboard_resource);
struct tw_pointer *
tw_seat_new_pointer(struct tw_seat *seat);

void
tw_seat_remove_pointer(struct tw_seat *seat);

struct tw_touch *
tw_seat_new_touch(struct tw_seat *seat);

void
tw_seat_remove_touch(struct tw_seat *seat);

struct tw_seat_client *
tw_seat_client_find(struct tw_seat *seat, struct wl_client *client);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
