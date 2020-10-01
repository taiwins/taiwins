/*
 * input_device.c - taiwins server input device implementation
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

#include "input_device.h"
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

static void
input_device_destroy_dummy(struct tw_input_device *device)
{
}

void
tw_input_device_init(struct tw_input_device *device,
                     enum tw_input_device_type type,
                     void (*destroy)(struct tw_input_device *))
{
	destroy = (destroy) ? destroy : input_device_destroy_dummy;

	//does keyboard here need to have the keymaps, keystate anyway?
	//we need to check the code of libinput it should work the same
	switch (type) {
	case TW_INPUT_TYPE_KEYBOARD:
		device->input.keyboard.keymap = NULL;
		device->input.keyboard.keystate = NULL;
		device->input.keyboard.led_mask = 0;
		device->input.keyboard.mod_mask = 0;
		break;
	case TW_INPUT_TYPE_POINTER:
		break;
	case TW_INPUT_TYPE_TOUCH:
		break;
	case TW_INPUT_TYPE_TABLET_PAD:
		break;
	case TW_INPUT_TYPE_TABLET_TOOL:
		break;
	case TW_INPUT_TYPE_SWITCH:
		break;
	}
	wl_list_init(&device->link);
	device->seat_id = 0;
	device->emitter = NULL;
	device->destroy = destroy;
}

void
tw_input_device_fini(struct tw_input_device *device)
{
	if (device->type == TW_INPUT_TYPE_KEYBOARD) {
		xkb_state_unref(device->input.keyboard.keystate);
		xkb_keymap_unref(device->input.keyboard.keymap);

		device->input.keyboard.keymap  = NULL;
		device->input.keyboard.keystate  = NULL;
	}
	wl_list_remove(&device->link);
	if (device->emitter)
		wl_signal_emit(&device->emitter->remove, device);
	device->destroy(device);
}

void
tw_input_device_attach_emitter(struct tw_input_device *device,
                               struct tw_input_source *emitter)
{
	device->emitter = emitter;
}

void
tw_input_source_init(struct tw_input_source *source)
{
	wl_signal_init(&source->remove);
	//keyboard
	wl_signal_init(&source->keyboard.key);
	wl_signal_init(&source->keyboard.modifiers);
	wl_signal_init(&source->keyboard.keymap);
	//pointer
	wl_signal_init(&source->pointer.motion);
	wl_signal_init(&source->pointer.motion_absolute);
	wl_signal_init(&source->pointer.button);
	wl_signal_init(&source->pointer.axis);
	wl_signal_init(&source->pointer.frame);
	wl_signal_init(&source->pointer.swipe_begin);
	wl_signal_init(&source->pointer.swipe_update);
	wl_signal_init(&source->pointer.swipe_end);
	wl_signal_init(&source->pointer.pinch_begin);
	wl_signal_init(&source->pointer.pinch_update);
	wl_signal_init(&source->pointer.pinch_end);
	//touch
	wl_signal_init(&source->touch.down);
	wl_signal_init(&source->touch.up);
	wl_signal_init(&source->touch.motion);
	wl_signal_init(&source->touch.cancel);
}

void
tw_input_device_set_keymap(struct tw_input_device *device,
                           struct xkb_keymap *keymap)
{
	if (device->type != TW_INPUT_TYPE_KEYBOARD)
		return;
	xkb_state_unref(device->input.keyboard.keystate);
	xkb_keymap_unref(device->input.keyboard.keymap);

        device->input.keyboard.keymap = xkb_keymap_ref(keymap);
        device->input.keyboard.keystate =
	        xkb_state_new(device->input.keyboard.keymap);
}
