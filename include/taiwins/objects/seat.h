/*
 * seat.h - taiwins server wl_seat implementation
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

struct tw_cursor;
struct tw_seat;
struct tw_seat_client;

enum TW_KEYBOARD_MODIFIER {
	TW_MODIFIER_CTRL = (1 << 0),
	TW_MODIFIER_ALT = (1 << 1),
	TW_MODIFIER_SUPER = (1 << 2),
	TW_MODIFIER_SHIFT = (1 << 3),
};

enum TW_KEYBOARD_LED {
	TW_LED_NUM_LOCK = (1 << 0),
	TW_LED_CAPS_LOCK = (1 << 1),
	TW_LED_SCROLL_LOCK = (1 << 2),
};

struct tw_seat_pointer_grab;

struct tw_pointer_grab_interface {
	void (*enter)(struct tw_seat_pointer_grab *grab,
	              struct wl_resource *surface, double sx, double sy);
	void (*motion)(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
	               double sx, double sy);
	void (*button)(struct tw_seat_pointer_grab *grab,
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
	                  uint32_t mods_depressed, uint32_t mods_latched,
	                  uint32_t mods_locked, uint32_t group);
	void (*cancel)(struct tw_seat_keyboard_grab *grab);
};

struct tw_seat_touch_grab;

struct tw_touch_grab_interface {
	void (*down)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	                 uint32_t touch_id, double sx, double sy);
	void (*up)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	           uint32_t touch_id);
	void (*motion)(struct tw_seat_touch_grab *grab, uint32_t time_msec,
	               uint32_t touch_id, double sx, double sy);
	void (*enter)(struct tw_seat_touch_grab *grab,
	              struct wl_resource *surface, double sx, double sy);
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

struct tw_event_new_cursor {
	struct wl_resource *surface;
	struct wl_resource *pointer;
	uint32_t hotspot_x;
	uint32_t hotspot_y;
};

struct tw_keyboard {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;
	struct wl_listener focused_destroy;

	size_t keymap_size;
	char *keymap_string;
	uint32_t modifiers_state;
	uint32_t led_state; /**< led state reflects lock state */

	struct tw_seat_keyboard_grab default_grab;
	struct tw_seat_keyboard_grab *grab;
};

struct tw_pointer {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;
	struct wl_listener focused_destroy;

	struct tw_seat_pointer_grab default_grab;
	struct tw_seat_pointer_grab *grab;
	uint32_t btn_count;
};

struct tw_touch {
	struct tw_seat_client *focused_client;
	struct wl_resource *focused_surface;
	struct wl_listener focused_destroy;

	struct tw_seat_touch_grab default_grab;
	struct tw_seat_touch_grab *grab;
};

struct tw_seat {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list clients;
	struct wl_list link;
	/** exotic resources like input-method and text-input */
	struct wl_list resources;
	char name[32];

	uint32_t capabilities;
	uint32_t repeat_delay, repeat_rate;
	uint32_t last_pointer_serial;
	uint32_t last_touch_serial;
	uint32_t last_keyboard_serial;
	struct tw_keyboard keyboard;
	struct tw_pointer pointer;
	struct tw_touch touch;
	struct tw_cursor *cursor;

	struct {
		struct wl_signal focus; /**< notify focused device */
		struct wl_signal destroy;
	} signals;
};

struct tw_seat_client {
	struct tw_seat *seat;
	struct wl_client *client;
	struct wl_list link;
	struct wl_list resources;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touches;
};

//seat API, implement wl_seat on the server side.
struct tw_seat *
tw_seat_create(struct wl_display *display, struct tw_cursor *cursor,
               const char *name);
void
tw_seat_destroy(struct tw_seat *seat);

struct tw_seat *
tw_seat_from_resource(struct wl_resource *resource);

void
tw_seat_set_name(struct tw_seat *seat, const char *name);

void
tw_seat_set_key_repeat_rate(struct tw_seat *seat, uint32_t delay,
                            uint32_t rate);
void
tw_seat_send_capabilities(struct tw_seat *seat);

struct tw_seat_client *
tw_seat_client_find(struct tw_seat *seat, struct wl_client *client);

struct tw_seat_client *
tw_seat_client_from_device(struct wl_resource *resource);

bool
tw_seat_valid_serial(struct tw_seat *seat, uint32_t serial);

/******************************** keyboard ***********************************/

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
void
tw_keyboard_set_focus(struct tw_keyboard *keyboard,
                      struct wl_resource *wl_surface,
                      struct wl_array *focus_keys);
void
tw_keyboard_clear_focus(struct tw_keyboard *keyboard);

void
tw_keyboard_notify_enter(struct tw_keyboard *keyboard,
                         struct wl_resource *surface, uint32_t keycodes[],
                         size_t n_keycodes);
void
tw_keyboard_notify_key(struct tw_keyboard *keyboard, uint32_t time_msec,
                       uint32_t key, uint32_t state);
void
tw_keyboard_notify_modifiers(struct tw_keyboard *keyboard,
                             uint32_t mods_depressed, uint32_t mods_latched,
                             uint32_t mods_locked, uint32_t group);

/***************************** pointer ***************************************/

struct tw_pointer *
tw_seat_new_pointer(struct tw_seat *seat);

void
tw_seat_remove_pointer(struct tw_seat *seat);

void
tw_pointer_start_grab(struct tw_pointer *pointer,
                      struct tw_seat_pointer_grab *grab);
void
tw_pointer_end_grab(struct tw_pointer *pointer);

void
tw_pointer_set_focus(struct tw_pointer *pointer,
                     struct wl_resource *wl_surface,
                     double sx, double sy);
void
tw_pointer_clear_focus(struct tw_pointer *pointer);

void
tw_pointer_notify_enter(struct tw_pointer *pointer,
                        struct wl_resource *wl_surface,
                        double sx, double sy);
void
tw_pointer_notify_motion(struct tw_pointer *pointer, uint32_t time_msec,
                         double sx, double sy);
void
tw_pointer_notify_button(struct tw_pointer *pointer, uint32_t time_msec,
                         uint32_t button, enum wl_pointer_button_state state);
void
tw_pointer_notify_axis(struct tw_pointer *pointer, uint32_t time_msec,
                       enum wl_pointer_axis axis, double val, int val_disc,
                       enum wl_pointer_axis_source source);
void
tw_pointer_notify_frame(struct tw_pointer *pointer);

/***************************** touch *****************************************/

struct tw_touch *
tw_seat_new_touch(struct tw_seat *seat);

void
tw_seat_remove_touch(struct tw_seat *seat);

void
tw_touch_start_grab(struct tw_touch *touch,
                    struct tw_seat_touch_grab *grab);
void
tw_touch_end_grab(struct tw_touch *touch);

void
tw_touch_set_focus(struct tw_touch *touch,
                     struct wl_resource *wl_surface,
                     double sx, double sy);
void
tw_touch_clear_focus(struct tw_touch *touch);

void
tw_touch_notify_down(struct tw_touch *touch, uint32_t time_msec, uint32_t id,
                     double sx, double sy);
void
tw_touch_notify_up(struct tw_touch *touch, uint32_t time_msec,
                   uint32_t touch_id);
void
tw_touch_notify_motion(struct tw_touch *touch, uint32_t time_msec,
                       uint32_t touch_id, double sx, double sy);
void
tw_touch_notify_enter(struct tw_touch *touch,
                      struct wl_resource *surface, double sx, double sy);
void
tw_touch_notify_cancel(struct tw_touch *touch);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
