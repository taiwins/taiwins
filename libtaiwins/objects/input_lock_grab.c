/*
 * input_lock_grab.c - taiwins server input lock grab implementation
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

#include <stdint.h>
#include <wayland-server.h>
#include <taiwins/objects/input_lock_grab.h>
#include <taiwins/objects/seat.h>
#include <wayland-util.h>

/******************************************************************************
 * keyboard grab
 *****************************************************************************/

static void
notify_lock_grab_keyboard_enter(struct tw_seat_keyboard_grab *grab,
                                struct wl_resource *resource,
                                uint32_t pressed[], size_t n_pressed)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_keyboard *keyboard = &grab->seat->keyboard;
	struct tw_surface *ref =
		resource ? tw_surface_from_resource(resource) : NULL;
	//resetting surface to lock surface
	keyboard->default_grab.impl->enter(&keyboard->default_grab,
	                                   lock->locked->resource,
	                                   pressed, n_pressed);
	lock->ref = ref;
}

static const struct tw_keyboard_grab_interface keyboard_iface = {
	.enter = notify_lock_grab_keyboard_enter,
	.key = tw_keyboard_default_key,
	.modifiers = tw_keyboard_default_modifiers,
};

/******************************************************************************
 * pointer grab
 *****************************************************************************/

static inline void
remap_surface_pos(struct tw_surface *ref, struct tw_surface *now,
                  float *x, float *y)
{
	tw_surface_to_global_pos(ref, *x, *y, x, y);
	tw_surface_to_local_pos(now, *x, *y, x, y);
}

static void
notify_lock_grab_pointer_enter(struct tw_seat_pointer_grab *grab,
                               struct wl_resource *resource,
                               double sx, double sy)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_surface *ref =
		resource ? tw_surface_from_resource(resource) : NULL;
	float x = sx, y = sy;

	if (ref && ref != lock->locked)
		remap_surface_pos(ref, lock->locked, &x, &y);

	pointer->default_grab.impl->enter(&pointer->default_grab,
	                                  lock->locked->resource, x, y);
	lock->ref = ref;
}

static void
notify_lock_grab_pointer_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_surface *ref = lock->ref;
	float x = sx, y = sy;

	if (ref && ref != lock->locked)
		remap_surface_pos(ref, lock->locked, &x, &y);
	pointer->default_grab.impl->motion(&pointer->default_grab,
	                                   time_msec, x, y);
}

static const struct tw_pointer_grab_interface pointer_iface = {
	.enter = notify_lock_grab_pointer_enter,
	.motion = notify_lock_grab_pointer_motion,
	.button = tw_pointer_default_button,
	.axis = tw_pointer_default_axis,
	.frame = tw_pointer_default_frame,
};

/******************************************************************************
 * touch grab
 *****************************************************************************/

static void
notify_lock_grab_touch_enter(struct tw_seat_touch_grab *grab,
                             struct wl_resource *resource,
                             double sx, double sy)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_touch *touch = &grab->seat->touch;
	struct tw_surface *ref =
		resource ? tw_surface_from_resource(resource) : NULL;
	float x = sx, y = sy;

	if (ref && ref != lock->locked)
		remap_surface_pos(ref, lock->locked, &x, &y);
	touch->default_grab.impl->enter(&touch->default_grab,
	                                lock->locked->resource, x, y);
	lock->ref = ref;
}

static void
notify_lock_grab_touch_down(struct tw_seat_touch_grab *grab,
                            uint32_t time_msec,
                            uint32_t touch_id, double sx, double sy)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_touch *touch = &grab->seat->touch;
	struct tw_surface *ref = lock->ref;
	float x = sx, y = sy;

	if (ref != lock->locked)
		remap_surface_pos(ref, lock->locked, &x, &y);
	touch->default_grab.impl->down(&touch->default_grab,
	                               time_msec, touch_id, x, y);
}

static void
notify_lock_grab_touch_motion(struct tw_seat_touch_grab *grab,
                              uint32_t time_msec,
                              uint32_t touch_id, double sx, double sy)
{
	struct tw_input_lock_grab *lock = grab->data;
	struct tw_touch *touch = &grab->seat->touch;
	struct tw_surface *ref = lock->ref;
	float x = sx, y = sy;

	if (ref != lock->locked)
		remap_surface_pos(ref, lock->locked, &x, &y);
	touch->default_grab.impl->motion(&touch->default_grab,
	                                 time_msec, touch_id, x, y);
}

static const struct tw_touch_grab_interface touch_iface = {
	.down = notify_lock_grab_touch_down,
	.up = tw_touch_default_up,
	.motion = notify_lock_grab_touch_motion,
	.enter = notify_lock_grab_touch_enter,
	.touch_cancel = tw_touch_default_touch_cancel,
};


/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_lock_grab_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_input_lock_grab *grab =
		wl_container_of(listener, grab, listeners.surface_destroy);

	tw_input_lock_grab_end(grab);
}

static void
notify_lock_grab_seat_destroy(struct wl_listener *listener, void *data)
{
	struct tw_input_lock_grab *grab =
		wl_container_of(listener, grab, listeners.seat_destroy);

	tw_input_lock_grab_end(grab);
}

/******************************************************************************
 * exposed APIs
 *****************************************************************************/

WL_EXPORT void
tw_input_lock_grab_start(struct tw_input_lock_grab *grab,
                         struct tw_seat *seat, struct tw_surface *surface)
{
	grab->seat = seat;
	grab->locked = surface;
	grab->ref = surface;
	wl_signal_init(&grab->grab_end);

	grab->keyboard_grab.data = grab;
	grab->keyboard_grab.impl = &keyboard_iface;
	grab->pointer_grab.data = grab;
	grab->pointer_grab.impl = &pointer_iface;
	grab->touch_grab.data = grab;
	grab->touch_grab.impl = &touch_iface;

	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		tw_keyboard_start_grab(&seat->keyboard, &grab->keyboard_grab,
		                       TW_INPUT_LOCK_GRAB_ORDER);
		tw_keyboard_notify_enter(&seat->keyboard, surface->resource,
		                         NULL, 0);
	}

	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) {
		tw_pointer_start_grab(&seat->pointer, &grab->pointer_grab,
		                      TW_INPUT_LOCK_GRAB_ORDER);
		tw_pointer_notify_enter(&seat->pointer, surface->resource,
		                        0.0f, 0.0f); //TODO correcting pos here
	}

	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		tw_touch_start_grab(&seat->touch, &grab->touch_grab,
		                    TW_INPUT_LOCK_GRAB_ORDER);
		tw_touch_notify_enter(&seat->touch, surface->resource,
		                      0.0f, 0.0f); //TODO correct pos here
	}

	tw_set_resource_destroy_listener(surface->resource,
	                                 &grab->listeners.surface_destroy,
	                                 notify_lock_grab_surface_destroy);
	tw_signal_setup_listener(&seat->signals.destroy,
	                         &grab->listeners.seat_destroy,
	                         notify_lock_grab_seat_destroy);
}

WL_EXPORT void
tw_input_lock_grab_end(struct tw_input_lock_grab *grab)
{
	struct tw_seat *seat = grab->seat;

	tw_reset_wl_list(&grab->listeners.seat_destroy.link);
	tw_reset_wl_list(&grab->listeners.surface_destroy.link);

	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER)
		tw_pointer_end_grab(&seat->pointer, &grab->pointer_grab);
	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		tw_keyboard_end_grab(&seat->keyboard, &grab->keyboard_grab);
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)
		tw_touch_end_grab(&seat->touch, &grab->touch_grab);

	grab->seat = NULL;
	grab->locked = NULL;
	wl_signal_emit(&grab->grab_end, grab);
}
