/*
 * seat_grab.h - taiwins server wl_seat grabs
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

#ifndef TW_SEAT_GRAB_H
#define TW_SEAT_GRAB_H

#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

/******************************************************************************
 * keyboard
 *****************************************************************************/

struct tw_keyboard;

struct tw_seat_keyboard_grab {
	const struct tw_keyboard_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
	struct wl_list link; /* keyboard:grabs */
};

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
	/* used on grab stack pop */
	void (*restart)(struct tw_seat_keyboard_grab *grab);
};

void
tw_keyboard_default_enter(struct tw_seat_keyboard_grab *grab,
                          struct wl_resource *surface, uint32_t *keycodes,
                          size_t n_keycodes);
void
tw_keyboard_default_key(struct tw_seat_keyboard_grab *grab,
                        uint32_t time_msec, uint32_t key, uint32_t state);
void
tw_keyboard_default_modifiers(struct tw_seat_keyboard_grab *grab,
                              uint32_t mods_depressed,
                              uint32_t mods_latched,
                              uint32_t mods_locked, uint32_t group);
void
tw_keyboard_default_cancel(struct tw_seat_keyboard_grab *grab);

/******************************************************************************
 * pointer
 *****************************************************************************/

struct tw_pointer;

struct tw_seat_pointer_grab {
	const struct tw_pointer_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
	struct wl_list link; /* touch:grabs */
};

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
	/* used on grab stack pop */
	void (*restart)(struct tw_seat_pointer_grab *grab);
};

void
tw_pointer_default_enter(struct tw_seat_pointer_grab *grab,
              struct wl_resource *surface, double sx, double sy);
void
tw_pointer_default_motion(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec, double sx, double sy);
void
tw_pointer_default_button(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec, uint32_t button,
                          enum wl_pointer_button_state state);
void
tw_pointer_default_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                        enum wl_pointer_axis orientation, double value,
                        int32_t value_discrete,
                        enum wl_pointer_axis_source source);
void
tw_pointer_default_frame(struct tw_seat_pointer_grab *grab);

void
tw_pointer_default_cancel(struct tw_seat_pointer_grab *grab);

/******************************************************************************
 * touch
 *****************************************************************************/

struct tw_keyboard;

struct tw_seat_touch_grab {
	const struct tw_touch_grab_interface *impl;
	struct tw_seat *seat;
	void *data;
	struct wl_list link; /* touch:grabs */
};

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
	/* used on grab stack pop */
	void (*restart)(struct tw_seat_touch_grab *grab);
};

void
tw_touch_default_down(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                           uint32_t touch_id, double sx, double sy);

void
tw_touch_default_up(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                    uint32_t touch_id);
void
tw_touch_default_motion(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                        uint32_t touch_id, double sx, double sy);
void
tw_touch_default_enter(struct tw_seat_touch_grab *grab,
                       struct wl_resource *surface, double sx, double sy);
void
tw_touch_default_touch_cancel(struct tw_seat_touch_grab *grab);

void
tw_touch_default_cancel(struct tw_seat_touch_grab *grab);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
