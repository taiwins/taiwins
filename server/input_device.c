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
	device->destroy(device);
}

void
tw_input_device_attach_emitter(struct tw_input_device *device,
                               union tw_input_source *emitter)
{
	device->emitter = emitter;
}
