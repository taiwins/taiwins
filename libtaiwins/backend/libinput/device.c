/*
 * device.c - taiwins server input device libinput implemenation
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

#include "taiwins/objects/utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libinput.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#include <taiwins/backend.h>
#include <taiwins/backend_libinput.h>
#include <taiwins/input_device.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>

/******************************************************************************
 * device events
 *****************************************************************************/

static void
handle_device_keyboard_event(struct tw_libinput_device *dev,
                             struct libinput_event_keyboard *event)
{
	struct tw_event_keyboard_key key;

	if (!event)
		return;
	//handle key event
	key.keycode = libinput_event_keyboard_get_key(event);
	key.dev = &dev->base;
	key.state = libinput_event_keyboard_get_key_state(event) ==
		LIBINPUT_KEY_STATE_PRESSED ? WL_KEYBOARD_KEY_STATE_PRESSED :
		WL_KEYBOARD_KEY_STATE_RELEASED;
	key.time = libinput_event_keyboard_get_time(event);

	tw_input_device_notify_key(&dev->base, &key);
}

static void
handle_device_pointer_motion_event(struct tw_libinput_device *dev,
                                   struct libinput_event_pointer *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

        if (emitter && event) {
		struct tw_event_pointer_motion motion = {
			.dev = &dev->base,
			.time = libinput_event_pointer_get_time(event),
			.delta_x = libinput_event_pointer_get_dx(event),
			.delta_y = libinput_event_pointer_get_dy(event),
			.unaccel_dx =
			libinput_event_pointer_get_dx_unaccelerated(event),
			.unaccel_dy =
			libinput_event_pointer_get_dy_unaccelerated(event),
		};

		wl_signal_emit(&emitter->pointer.motion, &motion);
		wl_signal_emit(&emitter->pointer.frame, &dev->base);
        }
}

static void
handle_device_pointer_motion_abs_event(struct tw_libinput_device *dev,
                                       struct libinput_event_pointer *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

        if (emitter && event) {
		struct tw_event_pointer_motion_abs motion = {
			.dev = &dev->base,
			.time_msec = libinput_event_pointer_get_time(event),
			.x = libinput_event_pointer_get_absolute_x_transformed(
				event, 1),
			.y = libinput_event_pointer_get_absolute_y_transformed(
				event, 1),
		};

		wl_signal_emit(&emitter->pointer.motion_absolute, &motion);
		wl_signal_emit(&emitter->pointer.frame, &dev->base);
        }
}

static void
handle_device_pointer_button_event(struct tw_libinput_device *dev,
                                   struct libinput_event_pointer *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

        if (emitter && event) {
		enum wl_pointer_button_state state =
			libinput_event_pointer_get_button_state(event) ==
			LIBINPUT_BUTTON_STATE_PRESSED ?
			WL_POINTER_BUTTON_STATE_PRESSED :
			WL_POINTER_BUTTON_STATE_RELEASED;
		struct tw_event_pointer_button button = {
			.dev = &dev->base,
			.state = state,
			.button = libinput_event_pointer_get_button(event),
			.time = libinput_event_pointer_get_time(event),
		};
		wl_signal_emit(&emitter->pointer.button, &button);
		wl_signal_emit(&emitter->pointer.frame, &dev->base);
        }
}

static void
handle_device_pointer_axis_event(struct tw_libinput_device *dev,
                                 struct libinput_event_pointer *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_axis axis = {0};

	if (!emitter || !event)
		return;
	axis.dev = &dev->base;
	axis.time = libinput_event_pointer_get_time(event);
	switch (libinput_event_pointer_get_axis_source(event)) {
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
		axis.source = WL_POINTER_AXIS_SOURCE_WHEEL;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
		axis.source = WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
		axis.source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
		axis.source = WL_POINTER_AXIS_SOURCE_FINGER;
		break;
	}
	if (libinput_event_pointer_has_axis(
		    event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
		axis.axis = WL_POINTER_AXIS_HORIZONTAL_SCROLL;
		axis.delta = libinput_event_pointer_get_axis_value(
			event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
		axis.delta_discrete =
			libinput_event_pointer_get_axis_value_discrete(
				event,LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
		wl_signal_emit(&emitter->pointer.axis, &axis);

	} else if (libinput_event_pointer_has_axis(
		           event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
		axis.axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
		axis.delta = libinput_event_pointer_get_axis_value(
			event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		axis.delta_discrete =
			libinput_event_pointer_get_axis_value_discrete(
				event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		wl_signal_emit(&emitter->pointer.axis, &axis);

	}
	wl_signal_emit(&emitter->pointer.frame, &dev->base);
}

static void
handle_device_touch_down_event(struct tw_libinput_device *dev,
                               struct libinput_event_touch *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

	if (emitter && event) {
		//TODO: no monitor infomation, need to query from backend.
		struct tw_event_touch_down down = {
			.dev = &dev->base,
			.time = libinput_event_touch_get_time(event),
			.touch_id = libinput_event_touch_get_seat_slot(event),
			.x = libinput_event_touch_get_x_transformed(event, 1),
			.y = libinput_event_touch_get_y_transformed(event, 1),
		};
		wl_signal_emit(&emitter->touch.down, &down);
	}
}

static void
handle_device_touch_up_event(struct tw_libinput_device *dev,
                             struct libinput_event_touch *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

	if (emitter && event) {
		//TODO: no monitor infomation, need to query from backend.
		struct tw_event_touch_motion motion = {
			.dev = &dev->base,
			.time = libinput_event_touch_get_time(event),
			.touch_id = libinput_event_touch_get_seat_slot(event),
			.x = libinput_event_touch_get_x_transformed(event, 1),
			.y = libinput_event_touch_get_y_transformed(event, 1),
		};
		wl_signal_emit(&emitter->touch.motion, &motion);
	}
}

static void
handle_device_touch_motion_event(struct tw_libinput_device *dev,
                                 struct libinput_event_touch *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	if (emitter && event) {
		struct tw_event_touch_up up = {
			.dev = &dev->base,
			.time = libinput_event_touch_get_time(event),
			.touch_id = libinput_event_touch_get_seat_slot(event),
		};
		wl_signal_emit(&emitter->touch.up, &up);
	}
}

/******************************************************************************
 * input device creation/destruction
 *****************************************************************************/

static inline uint32_t
parse_libinput_seat_id(struct libinput_seat *seat)
{
	uint32_t id = 0;
	const char *seat_name = libinput_seat_get_physical_name(seat);

	if (seat_name && (sscanf(seat_name, "seat%u", &id) == 1))
		return id;
	return 0;
}

static bool
tw_input_device_type_from_libinput(struct libinput_device *device,
                                   enum tw_input_device_type *type)
{
	if (libinput_device_has_capability(
		    device, LIBINPUT_DEVICE_CAP_KEYBOARD))
		*type = TW_INPUT_TYPE_KEYBOARD;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_POINTER))
		*type = TW_INPUT_TYPE_POINTER;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TOUCH))
		*type = TW_INPUT_TYPE_TOUCH;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_GESTURE))
		*type = TW_INPUT_TYPE_POINTER;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_SWITCH))
		*type = TW_INPUT_TYPE_SWITCH;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TABLET_PAD))
		*type = TW_INPUT_TYPE_TABLET_PAD;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
		*type = TW_INPUT_TYPE_TABLET_TOOL;
	else
		return false;
	return true;
}

static void
handle_libinput_device_remove(struct tw_input_device *base)
{
	struct tw_libinput_device *device =
		wl_container_of(base, device, base);
	libinput_device_unref(device->libinput);
}

static const struct tw_input_device_impl libinput_device_impl = {
	.destroy = handle_libinput_device_remove,
};

static struct tw_libinput_device *
tw_libinput_device_new(struct libinput_device *libinput_dev,
                       struct tw_libinput_input *input)
{
	struct tw_libinput_device *dev = NULL;
	struct libinput_seat *seat = libinput_device_get_seat(libinput_dev);
	uint32_t seat_id = parse_libinput_seat_id(seat);
	const char *name = libinput_device_get_name(libinput_dev);
	enum tw_input_device_type type;

	bool valid = tw_input_device_type_from_libinput(libinput_dev, &type);

	if (!valid)
		return NULL;
	if (!(dev = calloc(1, sizeof(*dev))))
		return NULL;

	tw_input_device_init(&dev->base, type, seat_id, &libinput_device_impl);
	strncpy(dev->base.name, (name) ? name : "<unknown>",
	        sizeof(dev->base.name));

	wl_list_init(&dev->link);
	dev->base.vendor = libinput_device_get_id_vendor(libinput_dev);
	dev->base.product = libinput_device_get_id_product(libinput_dev);
	dev->input = input;
	dev->libinput = libinput_dev;
	libinput_device_set_user_data(libinput_dev, dev);
	libinput_device_ref(libinput_dev);

	wl_list_insert(input->devices.prev, &dev->link);
	wl_list_insert(input->backend->inputs.prev, &dev->base.link);
	if (input->backend->started)
		wl_signal_emit(&input->backend->events.new_input, &dev->base);

	return dev;
}

static inline void
tw_libinput_device_destroy(struct tw_libinput_device *dev)
{
	if (!dev)
		return;
	wl_list_remove(&dev->link);
	tw_input_device_fini(&dev->base);
	free(dev);
}

/******************************************************************************
 * wayland event
 *****************************************************************************/

static void
handle_device_event(struct libinput_event *event)
{
	struct libinput_device *libinput_device =
		libinput_event_get_device(event);
	struct tw_libinput_device *dev =
		libinput_device_get_user_data(libinput_device);

        if (!dev)
		return;
	assert(dev->libinput == libinput_device &&
	       dev->base.impl == &libinput_device_impl);

        switch(libinput_event_get_type(event)) {
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		handle_device_keyboard_event(
			dev, libinput_event_get_keyboard_event(event));
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		handle_device_pointer_motion_event(
			dev, libinput_event_get_pointer_event(event));
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		handle_device_pointer_motion_abs_event(
			dev, libinput_event_get_pointer_event(event));
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		handle_device_pointer_button_event(
			dev, libinput_event_get_pointer_event(event));
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		handle_device_pointer_axis_event(
			dev, libinput_event_get_pointer_event(event));
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		handle_device_touch_down_event(
			dev, libinput_event_get_touch_event(event));
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		handle_device_touch_motion_event(
			dev, libinput_event_get_touch_event(event));
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		handle_device_touch_up_event(
			dev, libinput_event_get_touch_event(event));
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
	case LIBINPUT_EVENT_TABLET_PAD_RING:
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
	case LIBINPUT_EVENT_TABLET_PAD_KEY:
		break;
		//gestures need to be supported as it is in wayland protocol
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		break;
        case LIBINPUT_EVENT_SWITCH_TOGGLE:
	        break;
	default:
		return;
	}
}

static bool
handle_input_event(struct libinput_event *event)
{
	bool handled = true;
	struct libinput *libinput =
		libinput_event_get_context(event);
	struct libinput_device *libinput_device =
		libinput_event_get_device(event);
	struct tw_libinput_input *input =
		libinput_get_user_data(libinput);

	switch(libinput_event_get_type(event)) {

	case LIBINPUT_EVENT_DEVICE_ADDED:
		tw_libinput_device_new(libinput_device, input);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		tw_libinput_device_destroy(
			libinput_device_get_user_data(libinput_device));
		break;
	default:
		handled = false;
		break;
	}
	return handled;
}

static inline void
handle_events(struct tw_libinput_input *input)
{
	struct libinput_event *event;

	while ((event = libinput_get_event(input->libinput))) {
		if (!handle_input_event(event))
			handle_device_event(event);
		libinput_event_destroy(event);
	}
}

static int
handle_dispatch_libinput(int fd, uint32_t mask, void *data)
{
	struct tw_libinput_input *input = data;

	if (libinput_dispatch(input->libinput) != 0) {
		tw_logl_level(TW_LOG_WARN, "Failed to dispatch libinput");
		return 0;
	}
	handle_events(input);
	return 0;
}

/******************************************************************************
 * public API
 *****************************************************************************/

WL_EXPORT void
tw_libinput_input_init(struct tw_libinput_input *input,
                       struct tw_backend *backend, struct wl_display *display,
                       struct libinput *libinput)
{
	wl_list_init(&input->devices);
	input->display = display;
	input->libinput = libinput;
	input->backend = backend;
	libinput_set_user_data(libinput, input);

	return;
}

WL_EXPORT bool
tw_libinput_input_enable(struct tw_libinput_input *input, const char *seat)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(input->display);
	int fd = libinput_get_fd(input->libinput);

        if (libinput_udev_assign_seat(input->libinput, seat) != 0)
		return false;
        handle_events(input);

	input->event = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
	                                    handle_dispatch_libinput,
	                                    input);
	if (!input->event)
		return false;
	if (input->disabled) {
		libinput_resume(input->libinput);
		handle_events(input);
		input->disabled = false;
	}

	return true;
}

WL_EXPORT void
tw_libinput_input_disable(struct tw_libinput_input *input)
{
	if (input->disabled)
		return;
	wl_event_source_remove(input->event);
	input->event = NULL;
	libinput_suspend(input->libinput);
	handle_events(input);
	input->disabled = true;
}

WL_EXPORT void
tw_libinput_input_fini(struct tw_libinput_input *input)
{
	struct tw_libinput_device *dev, *dev_tmp;

	if (input->event) {
		wl_event_source_remove(input->event);
		input->event = NULL;
	}
	wl_list_for_each_safe(dev, dev_tmp, &input->devices, link)
		tw_libinput_device_destroy(dev);
}
