/*
 * seat.c - taiwins server wayland backend seat implementation
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

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/input_device.h>
#include <wayland-util.h>
#include "internal.h"
#include "taiwins/objects/utils.h"



static inline void
signal_new_input(struct tw_wl_backend *backend,
                       struct tw_input_device *dev)
{
	wl_signal_emit(&backend->base.events.new_input, dev);
}

/******************************************************************************
 * keyboard listeners
 *****************************************************************************/

static void
handle_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                       uint32_t format, int32_t fd, uint32_t size)
{
	close(fd);
	//NOT used
}

static void
handle_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		      uint32_t serial, struct wl_surface *surface,
		      struct wl_array *keys)
{
	struct tw_wl_seat *seat = wl_keyboard_get_user_data(wl_keyboard);
	struct tw_input_device *keyboard = &seat->keyboard_dev;
	uint32_t time = tw_get_time_msec();
	uint32_t *keycode;

	wl_array_for_each(keycode, keys) {
		struct tw_event_keyboard_key event = {
			.dev = keyboard,
			.state = WL_KEYBOARD_KEY_STATE_PRESSED,
			.time = time,
		};
		tw_input_device_notify_key(keyboard, &event);
	}
}

static void
handle_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
	//TODO: notify keycode release
}

static void
handle_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                    uint32_t serial, uint32_t time, uint32_t key,
                    uint32_t state)
{
	struct tw_wl_seat *seat = wl_keyboard_get_user_data(wl_keyboard);
	struct tw_input_device *keyboard = &seat->keyboard_dev;
	struct tw_event_keyboard_key event = {
		.dev = keyboard,
		.time = time,
		.state = state,
		.keycode = key,
	};
	tw_input_device_notify_key(keyboard, &event);
}

static void
handle_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                           uint32_t serial, uint32_t depressed,
                           uint32_t latched, uint32_t locked, uint32_t group)
{
	struct tw_wl_seat *seat = wl_keyboard_get_user_data(wl_keyboard);
	struct tw_input_device *keyboard = &seat->keyboard_dev;
	struct tw_event_keyboard_modifier event = {
		.dev = keyboard,
		.depressed = depressed,
		.latched = latched,
		.locked = locked,
		.group = group,
	};
	tw_input_device_notify_modifiers(keyboard, &event);

}

static void
handle_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                            int32_t rate, int32_t delay)
{
	//NOT USED
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = handle_keyboard_keymap,
	.enter = handle_keyboard_enter,
	.leave = handle_keyboard_leave,
	.key = handle_keyboard_key,
	.modifiers = handle_keyboard_modifiers,
	.repeat_info = handle_keyboard_repeat_info,
};

/******************************************************************************
 * pointer listeners
 *****************************************************************************/

static void
handle_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                     uint32_t serial, struct wl_surface *surface,
                     wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct tw_wl_output *output =
		wl_surface_get_user_data(surface);
	output->curr_pointer = wl_pointer;
	//hide the cursor, we render it internally
	wl_pointer_set_cursor(wl_pointer, serial, NULL, 0, 0);
}

static void
handle_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                     uint32_t serial, struct wl_surface *surface)
{
	struct tw_wl_output *output =
		wl_surface_get_user_data(surface);
	output->curr_pointer = NULL;
}

static void
handle_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                      wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct tw_wl_seat *seat = wl_pointer_get_user_data(wl_pointer);
	struct tw_input_device *pointer = &seat->pointer_dev;
	struct tw_event_pointer_motion_abs event = {
		.dev = pointer,
		.time_msec = time,
		.x = wl_fixed_to_double(surface_x),
		.y = wl_fixed_to_double(surface_y),

	};

	if (pointer->emitter)
		wl_signal_emit(&pointer->emitter->pointer.motion_absolute,
		               &event);
}

static void
handle_pointer_button(void *data, struct wl_pointer *wl_pointer,
                      uint32_t serial, uint32_t time, uint32_t button,
                      uint32_t state)
{
	struct tw_wl_seat *seat = wl_pointer_get_user_data(wl_pointer);
	struct tw_input_device *pointer = &seat->pointer_dev;
	struct tw_event_pointer_button event = {
		.dev = pointer,
		.time = time,
		.button = button,
		.state = state
	};

	if (pointer->emitter)
		wl_signal_emit(&pointer->emitter->pointer.button,
		               &event);
}

static void
handle_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value)
{
	struct tw_wl_seat *seat = wl_pointer_get_user_data(wl_pointer);
	struct tw_input_device *pointer = &seat->pointer_dev;
	struct tw_event_pointer_axis event = {
		.dev = pointer,
		.time = time,
		.source = WL_POINTER_AXIS_SOURCE_WHEEL,
		.axis = axis,
		.delta = wl_fixed_to_double(value),
		.delta_discrete = wl_fixed_to_int(value) > 0 ? 1 : -1,
	};

	if (pointer->emitter)
		wl_signal_emit(&pointer->emitter->pointer.axis, &event);
}

static void
handle_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
	struct tw_wl_seat *seat = wl_pointer_get_user_data(wl_pointer);
	struct tw_input_device *pointer = &seat->pointer_dev;

        if (pointer->emitter)
		wl_signal_emit(&pointer->emitter->pointer.frame, pointer);

}

static void
handle_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                           uint32_t axis_source)
{
	//NOT USED
}

static void
handle_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                         uint32_t time, uint32_t axis)
{
	//NOT USED
}

static void
handle_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                             uint32_t axis, int32_t discrete)
{
	//NOT USED
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = handle_pointer_enter,
	.leave = handle_pointer_leave,
	.motion = handle_pointer_motion,
	.button = handle_pointer_button,
	.axis = handle_pointer_axis,
	.frame = handle_pointer_frame,
	.axis_source = handle_pointer_axis_source,
	.axis_stop = handle_pointer_axis_stop,
	.axis_discrete = handle_pointer_axis_discrete,
};

/******************************************************************************
 * seat listeners
 *****************************************************************************/

static void
handle_seat_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
	struct tw_wl_seat *seat = data;

	//new keyboard
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !seat->wl_keyboard) {
		tw_logl_level(TW_LOG_DBUG, "seat %d offers keyboard",
		              seat->gid);
		seat->wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(seat->wl_keyboard, &keyboard_listener,
		                         seat);
		tw_input_device_init(&seat->keyboard_dev,
		                     TW_INPUT_TYPE_KEYBOARD, seat->name, NULL);
		strncpy(seat->keyboard_dev.name, "wl-keyboard",
		        sizeof(seat->keyboard_dev.name));
		if (seat->wl->base.started)
			signal_new_input(seat->wl, &seat->keyboard_dev);
	}
	//drop keyboard
	if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && seat->wl_keyboard) {
		tw_logl_level(TW_LOG_DBUG, "seat %d loses pointer",
		              seat->gid);
		wl_keyboard_release(seat->wl_keyboard);
		seat->wl_keyboard = NULL;
		tw_input_device_fini(&seat->keyboard_dev);
	}

	//new pointer
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !seat->wl_pointer) {
		tw_logl_level(TW_LOG_DBUG, "seat %d offers pointer",
		              seat->gid);
		seat->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->wl_pointer, &pointer_listener,
		                        seat);
		tw_input_device_init(&seat->pointer_dev, TW_INPUT_TYPE_POINTER,
		                     seat->name, NULL);
		strncpy(seat->pointer_dev.name, "wl-pointer",
		        sizeof(seat->pointer_dev.name));
		if (seat->wl->base.started)
			signal_new_input(seat->wl, &seat->pointer_dev);
		//TODO creating pointer for output?
	}
	//drop pointer
	if (!(caps & WL_SEAT_CAPABILITY_POINTER) && seat->wl_pointer) {
		tw_logl_level(TW_LOG_DBUG, "seat %d loses pointer",
		              seat->gid);
		wl_pointer_release(seat->wl_pointer);
		seat->wl_pointer = NULL;
		tw_input_device_fini(&seat->pointer_dev);
	}
	seat->caps = caps;
}

static void
handle_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	struct tw_wl_seat *seat = data;
	int id = 0;
	if (sscanf(name, "seat%d", &id) == 1) {
		seat->name = id;
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = handle_seat_capabilities,
	.name = handle_seat_name,
};

struct tw_wl_seat *
tw_wl_handle_new_seat(struct tw_wl_backend *wl, struct wl_registry *reg,
                      uint32_t id, uint32_t version)
{
	struct tw_wl_seat *seat = calloc(1, sizeof(*seat));
	if (!seat)
		return NULL;

	seat->wl = wl;
	seat->wl_seat = wl_registry_bind(reg, id, &wl_seat_interface, version);
	seat->name = wl_list_length(&wl->seats);
	seat->caps = 0;
	seat->gid = id;
	wl_list_init(&seat->link);

	wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);

	return seat;
}

void
tw_wl_seat_start(struct tw_wl_seat *seat)
{
	if (seat->wl_pointer)
		signal_new_input(seat->wl, &seat->pointer_dev);
	if (seat->wl_keyboard)
		signal_new_input(seat->wl, &seat->keyboard_dev);
}
