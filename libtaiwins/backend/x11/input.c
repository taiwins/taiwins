/*
 * backend.c - taiwins server x11 backend input implementation
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

#include <wayland-server.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/seat.h>

#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include "internal.h"

static void
handle_x11_key_event(struct tw_input_device *keyboard,
                     xcb_ge_generic_event_t *ge,
                     enum wl_keyboard_key_state state)
{
	xcb_input_key_press_event_t *ev =
		(xcb_input_key_press_event_t *)ge;
	struct tw_event_keyboard_key key = {
		.dev = keyboard,
		.time = ev->time,
		.keycode = ev->detail - 8,
		.state = state,
	};
	struct tw_event_keyboard_modifier mod = {
		.dev = keyboard,
		.depressed = ev->mods.base,
		.latched = ev->mods.latched,
		.locked = ev->mods.locked,
		.group = ev->mods.effective,
	};
	tw_input_device_notify_modifiers(keyboard, &mod);
	tw_input_device_notify_key(keyboard, &key);
}

static void
handle_x11_btn_axis(struct tw_x11_backend *x11,
                    xcb_ge_generic_event_t *ge,
                    enum wl_pointer_button_state state)
{
	struct tw_input_source *emitter;

	xcb_input_button_press_event_t *ev =
		(xcb_input_button_press_event_t *)ge;
	struct tw_x11_output *output =
		tw_x11_output_from_id(x11, ev->event);
	struct tw_event_pointer_button btn = {
		.dev = &output->pointer,
		.state = state,
		.time = ev->time,
	};
	struct tw_event_pointer_axis axis = {
		.dev = &output->pointer,
		.time = ev->time,
		.source = WL_POINTER_AXIS_SOURCE_WHEEL,
		.axis = WL_POINTER_AXIS_VERTICAL_SCROLL,
	};

	if (!output)
		return;
	emitter = output->pointer.emitter;

	if (ev->detail == XCB_BUTTON_INDEX_1)
		btn.button = BTN_LEFT;
	else if (ev->detail == XCB_BUTTON_INDEX_2)
		btn.button = BTN_MIDDLE;
	else if (ev->detail == XCB_BUTTON_INDEX_3)
		btn.button = BTN_RIGHT;
	else if (ev->detail == XCB_BUTTON_INDEX_4 && state)
		axis.delta_discrete = -1;
	else if (ev->detail == XCB_BUTTON_INDEX_5 && state)
		axis.delta_discrete = 1;

	axis.delta = 15.0 * axis.delta_discrete;

	if (emitter && btn.button != 0) {
		tw_input_signal_emit(emitter, pointer.button, &btn);
		wl_signal_emit(&emitter->pointer.frame, &output->pointer);
	} else if (emitter && axis.delta_discrete != 0) {
		tw_input_signal_emit(emitter, pointer.axis, &axis);
		wl_signal_emit(&emitter->pointer.frame, &output->pointer);
	}
}

static void
handle_x11_motion(struct tw_x11_backend *x11, xcb_ge_generic_event_t *ge)
{
	xcb_input_motion_event_t *ev = (xcb_input_motion_event_t *)ge;
	struct tw_x11_output *output = tw_x11_output_from_id(x11, ev->event);
	struct tw_event_pointer_motion_abs abs = {
		.dev = &output->pointer,
	};
	unsigned width, height;
	struct tw_input_source *emitter;

	if (!output) return;

	tw_output_device_raw_resolution(&output->output.device,
	                                &width, &height);

	emitter = output->pointer.emitter;

	abs.time_msec = ev->time;
	abs.x = (double)(ev->event_x >> 16) / (float)width;
	abs.y = (double)(ev->event_y >> 16) / (float)height;
	abs.output = &output->output.device;
	if (emitter) {
		tw_input_signal_emit(emitter, pointer.motion_absolute, &abs);
		wl_signal_emit(&emitter->pointer.frame, &output->pointer);
	}
}

static void
handle_x11_cursor_hideshow(struct tw_x11_backend *x11,
                           xcb_ge_generic_event_t *ge, bool show)
{
	xcb_input_enter_event_t *ev = (xcb_input_enter_event_t *)ge;
	struct tw_x11_output *output =
		tw_x11_output_from_id(x11, ev->event);
	if (!output)
		return;

	if (show)
		xcb_xfixes_show_cursor(x11->xcb_conn, output->win);
	else
		xcb_xfixes_hide_cursor(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);
}

void
tw_x11_handle_input_event(struct tw_x11_backend *x11,
                          xcb_ge_generic_event_t *ge)
{
	switch (ge->event_type) {
	case XCB_INPUT_KEY_PRESS:
		handle_x11_key_event(&x11->keyboard, ge,
		                     WL_KEYBOARD_KEY_STATE_PRESSED);
		break;

	case XCB_INPUT_KEY_RELEASE:
		handle_x11_key_event(&x11->keyboard, ge,
		                     WL_KEYBOARD_KEY_STATE_RELEASED);
		break;
	case XCB_INPUT_BUTTON_PRESS:
		handle_x11_btn_axis(x11, ge, WL_POINTER_BUTTON_STATE_PRESSED);
		break;
	case XCB_INPUT_BUTTON_RELEASE:
		handle_x11_btn_axis(x11, ge, WL_POINTER_BUTTON_STATE_RELEASED);
		break;
	case XCB_INPUT_MOTION:
		handle_x11_motion(x11, ge);
		break;
	case XCB_INPUT_ENTER:
		handle_x11_cursor_hideshow(x11, ge, false);
		break;
	case XCB_INPUT_LEAVE:
		handle_x11_cursor_hideshow(x11, ge, true);
		break;
	case XCB_INPUT_TOUCH_BEGIN:
		break;
	case XCB_INPUT_TOUCH_END:
		break;
	case XCB_INPUT_TOUCH_UPDATE:
		break;
	}
}
