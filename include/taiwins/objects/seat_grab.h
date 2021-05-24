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

#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif

/******************************************************************************
 * keyboard
 *****************************************************************************/

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

static inline void
tw_keyboard_notify_enter(struct tw_keyboard *keyboard,
                         struct wl_resource *surface, uint32_t *keycodes,
                         size_t n_keycodes)
{
	if (keyboard->grab && keyboard->grab->impl->enter)
		keyboard->grab->impl->enter(keyboard->grab,
		                            surface, keycodes, n_keycodes);
}

static inline void
tw_keyboard_notify_key(struct tw_keyboard *keyboard, uint32_t time_msec,
                       uint32_t key, uint32_t state)
{
	if (keyboard->grab && keyboard->grab->impl->key)
		keyboard->grab->impl->key(keyboard->grab, time_msec, key,
		                          state);
}

static inline void
tw_keyboard_notify_modifiers(struct tw_keyboard *keyboard,
                             uint32_t mods_depressed, uint32_t mods_latched,
                             uint32_t mods_locked, uint32_t group)
{
	if (keyboard->grab && keyboard->grab->impl->modifiers)
		keyboard->grab->impl->modifiers(keyboard->grab,
		                                mods_depressed, mods_latched,
		                                mods_locked, group);
}

/******************************************************************************
 * pointer
 *****************************************************************************/

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

static inline void
tw_pointer_notify_enter(struct tw_pointer *pointer,
                        struct wl_resource *wl_surface,
                        double sx, double sy)
{
	if (pointer->grab && pointer->grab->impl->enter)
		pointer->grab->impl->enter(pointer->grab, wl_surface, sx, sy);
}

static inline void
tw_pointer_notify_motion(struct tw_pointer *pointer, uint32_t time_msec,
                         double sx, double sy)
{
	if (pointer->grab && pointer->grab->impl->motion)
		pointer->grab->impl->motion(pointer->grab, time_msec, sx, sy);
}

static inline void
tw_pointer_notify_button(struct tw_pointer *pointer, uint32_t time_msec,
                         uint32_t button, enum wl_pointer_button_state state)
{
	if (pointer->grab && pointer->grab->impl->button)
		pointer->grab->impl->button(pointer->grab, time_msec,
		                                   button, state);
}

static inline void
tw_pointer_notify_axis(struct tw_pointer *pointer, uint32_t time_msec,
                       enum wl_pointer_axis axis, double val, int val_disc,
                       enum wl_pointer_axis_source source)
{
	if (pointer->grab && pointer->grab->impl->axis)
		pointer->grab->impl->axis(pointer->grab, time_msec, axis,
		                          val, val_disc, source);
}

static inline void
tw_pointer_notify_frame(struct tw_pointer *pointer)
{
	if (pointer->grab && pointer->grab->impl->frame)
		pointer->grab->impl->frame(pointer->grab);
}

/******************************************************************************
 * touch
 *****************************************************************************/

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

static inline void
tw_touch_notify_down(struct tw_touch *touch, uint32_t time_msec, uint32_t id,
                     double sx, double sy)
{
	if (touch->grab && touch->grab->impl->down)
		touch->grab->impl->down(touch->grab, time_msec, id,
		                        sx, sy);
}

static inline void
tw_touch_notify_up(struct tw_touch *touch, uint32_t time_msec,
                   uint32_t touch_id)
{
	if (touch->grab && touch->grab->impl->up)
		touch->grab->impl->up(touch->grab, time_msec, touch_id);
}

static inline void
tw_touch_notify_motion(struct tw_touch *touch, uint32_t time_msec,
                       uint32_t touch_id, double sx, double sy)
{
	if (touch->grab && touch->grab->impl->motion)
		touch->grab->impl->motion(touch->grab, time_msec, touch_id,
		                          sx, sy);
}

static inline void
tw_touch_notify_enter(struct tw_touch *touch,
                      struct wl_resource *surface, double sx, double sy)
{
	if (touch->grab && touch->grab->impl->enter)
		touch->grab->impl->enter(touch->grab, surface, sx, sy);
}

static inline void
tw_touch_notify_cancel(struct tw_touch *touch)
{
	if (touch->grab && touch->grab->impl->touch_cancel)
		touch->grab->impl->touch_cancel(touch->grab);
}

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
