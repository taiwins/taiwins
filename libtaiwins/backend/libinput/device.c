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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libinput.h>
#include <wayland-server.h>

#include <taiwins/backend.h>
#include <taiwins/backend_libinput.h>
#include <taiwins/input_device.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>

static inline struct tw_output_device *
request_output_device_from_libinput(struct tw_libinput_device *dev)
{
	struct tw_output_device *output_dev = NULL;
	struct udev_device *udev =
		libinput_device_get_udev_device(dev->libinput);

	if (dev->input->impl->get_output_device && udev)
		output_dev = dev->input->impl->get_output_device(udev);
	if (udev)
		udev_device_unref(udev);
	return output_dev;
}

/******************************************************************************
 * keyboard event
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

/******************************************************************************
 * pointer event
 *****************************************************************************/

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
			.output = request_output_device_from_libinput(dev),
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

/******************************************************************************
 * gesture event
 *****************************************************************************/

static void
handle_device_pinch_gesture_begin(struct tw_libinput_device *dev,
                                  struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_BEGIN;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.fingers = libinput_event_gesture_get_finger_count(event);

	wl_signal_emit(&emitter->pointer.pinch_begin, &gesture);
}

static void
handle_device_pinch_gesture_update(struct tw_libinput_device *dev,
                                   struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_UPDATE;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.dx = libinput_event_gesture_get_dx(event);
	gesture.dy = libinput_event_gesture_get_dy(event);
	gesture.scale = libinput_event_gesture_get_dy(event);
	gesture.rotation = libinput_event_gesture_get_angle_delta(event);

	wl_signal_emit(&emitter->pointer.pinch_update, &gesture);
}

static void
handle_device_pinch_gesture_end(struct tw_libinput_device *dev,
                                struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_END;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.cancelled = libinput_event_gesture_get_cancelled(event);

	wl_signal_emit(&emitter->pointer.pinch_end, &gesture);
}

static void
handle_device_swipe_gesture_begin(struct tw_libinput_device *dev,
                                  struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_BEGIN;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.fingers = libinput_event_gesture_get_finger_count(event);

	wl_signal_emit(&emitter->pointer.swipe_begin, &gesture);
}

static void
handle_device_swipe_gesture_update(struct tw_libinput_device *dev,
                                   struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_UPDATE;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.dx = libinput_event_gesture_get_dx(event);
	gesture.dy = libinput_event_gesture_get_dy(event);

	wl_signal_emit(&emitter->pointer.swipe_update, &gesture);
}

static void
handle_device_swipe_gesture_end(struct tw_libinput_device *dev,
                                struct libinput_event_gesture *event)
{
	struct tw_input_source *emitter = dev->base.emitter;
	struct tw_event_pointer_gesture gesture = {0};

	if (!emitter || !event)
		return;
	gesture.dev = &dev->base;
	gesture.state = TW_POINTER_GESTURE_END;
	gesture.time = libinput_event_gesture_get_time(event);
	gesture.cancelled = libinput_event_gesture_get_cancelled(event);

	wl_signal_emit(&emitter->pointer.swipe_end, &gesture);
}

/******************************************************************************
 * touch event
 *****************************************************************************/

static void
handle_device_touch_down_event(struct tw_libinput_device *dev,
                               struct libinput_event_touch *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

	if (emitter && event) {
		struct tw_event_touch_down down = {
			.dev = &dev->base,
			.time = libinput_event_touch_get_time(event),
			.touch_id = libinput_event_touch_get_seat_slot(event),
			.output = request_output_device_from_libinput(dev),
			.x = libinput_event_touch_get_x_transformed(event, 1),
			.y = libinput_event_touch_get_y_transformed(event, 1),
		};
		wl_signal_emit(&emitter->touch.down, &down);
	}
}

static void
handle_device_touch_motion_event(struct tw_libinput_device *dev,
                                 struct libinput_event_touch *event)
{
	struct tw_input_source *emitter = dev->base.emitter;

	if (emitter && event) {
		struct tw_event_touch_motion motion = {
			.dev = &dev->base,
			.time = libinput_event_touch_get_time(event),
			.touch_id = libinput_event_touch_get_seat_slot(event),
			.output = request_output_device_from_libinput(dev),
			.x = libinput_event_touch_get_x_transformed(event, 1),
			.y = libinput_event_touch_get_y_transformed(event, 1),
		};
		wl_signal_emit(&emitter->touch.motion, &motion);
	}
}

static void
handle_device_touch_up_event(struct tw_libinput_device *dev,
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
 * assembler
 *****************************************************************************/

void
handle_device_event(struct libinput_event *event)
{
	struct libinput_device *libinput_device =
		libinput_event_get_device(event);
	struct tw_libinput_device *dev =
		libinput_device_get_user_data(libinput_device);

        if (!dev)
		return;
        assert(dev->libinput == libinput_device);

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
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		handle_device_swipe_gesture_begin(
			dev, libinput_event_get_gesture_event(event));
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		handle_device_swipe_gesture_update(
			dev, libinput_event_get_gesture_event(event));
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		handle_device_swipe_gesture_end(
			dev, libinput_event_get_gesture_event(event));
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		handle_device_pinch_gesture_begin(
			dev, libinput_event_get_gesture_event(event));
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		handle_device_pinch_gesture_update(
			dev, libinput_event_get_gesture_event(event));
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		handle_device_pinch_gesture_end(
			dev, libinput_event_get_gesture_event(event));
		break;
	default:
		return;
	}
}
