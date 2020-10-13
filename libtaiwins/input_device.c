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

#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

#include <taiwins/input_device.h>


static void
input_device_destroy_dummy(struct tw_input_device *device)
{
}

static bool
update_keyboard_modifier(struct tw_input_device *dev)
{
	struct tw_keyboard_input *input = &dev->input.keyboard;
	struct xkb_state *state = dev->input.keyboard.keystate;
	xkb_mod_mask_t depressed, latched, locked, group;

	if (state == NULL) {
		return false;
	}

	depressed = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
	latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
	locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
	group = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

        if (depressed == input->depressed  && latched == input->latched &&
	    locked == input->locked && group == input->group) {
		return false;
	}

	input->depressed = depressed;
	input->latched = latched;
	input->locked = locked;
	input->group = group;

	return true;
}

static inline void
tw_keyboard_input_init(struct tw_keyboard_input *input)
{
	input->keymap = NULL;
	input->keystate = NULL;

	input->depressed = 0;
	input->latched = 0;
	input->locked = 0;
	input->group = 0;
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
		tw_keyboard_input_init(&device->input.keyboard);
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
	device->type = type;
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

void
tw_input_device_notify_key(struct tw_input_device *dev,
                           struct tw_event_keyboard_key *event)
{
	enum xkb_key_direction direction =
		event->state == WL_KEYBOARD_KEY_STATE_PRESSED ?
		XKB_KEY_DOWN : XKB_KEY_UP;
	bool updated = false;

	assert(dev->type == TW_INPUT_TYPE_KEYBOARD);

	if (dev->input.keyboard.keystate)
		xkb_state_update_key(dev->input.keyboard.keystate,
		                     event->keycode+8, direction);
	updated = update_keyboard_modifier(dev);

	if (dev->emitter)
		wl_signal_emit(&dev->emitter->keyboard.key, event);
	if (updated && dev->emitter) {
		struct tw_event_keyboard_modifier mods = {
			.dev = dev,
			.depressed = dev->input.keyboard.depressed,
			.latched = dev->input.keyboard.latched,
			.locked = dev->input.keyboard.locked,
			.group = dev->input.keyboard.group,
		};

		wl_signal_emit(&dev->emitter->keyboard.modifiers, &mods);
	}

}

void
tw_input_device_notify_modifiers(struct tw_input_device *dev,
                                 struct tw_event_keyboard_modifier *mod)
{
	bool updated = false;

	assert(dev->type == TW_INPUT_TYPE_KEYBOARD);
	if (dev->input.keyboard.keystate)
		xkb_state_update_mask(dev->input.keyboard.keystate,
		                      mod->depressed, mod->latched,
		                      mod->locked, 0, 0, mod->group);
	updated = update_keyboard_modifier(dev);

	if (updated && dev->emitter)
		wl_signal_emit(&dev->emitter->keyboard.modifiers, mod);
}
