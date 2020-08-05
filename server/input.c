/*
 * input.c - taiwins server input functions
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

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_input_device.h>

#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>

#include "backend.h"
#include "bindings.h"
#include "input.h"

static uint32_t
curr_modmask(struct tw_seat_events *seat_events)
{
	uint32_t mask = 0;
	struct  xkb_state *state = seat_events->keyboard_dev->xkb_state;

	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_SUPER;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_SHIFT;
	return mask;
}

static uint32_t
curr_ledmask(struct tw_seat_events *seat_events)
{
	uint32_t mask = 0;
	struct  xkb_state *state = seat_events->keyboard_dev->xkb_state;

	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_NUM))
		mask |= TW_LED_NUM_LOCK;
	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_CAPS))
		mask |= TW_LED_CAPS_LOCK;
	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_SCROLL))
		mask |= TW_LED_SCROLL_LOCK;
	return mask;
}

/******************************************************************************
 * bindings
 *
 * Binding grab used in taiwins. The grabs do the search for binding and calling
 * the corresponding binding if found.
 *
 ******************************************************************************/
static void
binding_key_cancel(struct tw_seat_keyboard_grab *grab)
{
	grab->data = NULL;
}

static void
binding_key(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
            uint32_t key, uint32_t state)
{
	struct tw_seat_events *seat_events =
		container_of(grab, struct tw_seat_events, binding_key_grab);
	struct tw_seat *seat = grab->seat;
	uint32_t mod_mask = curr_modmask(seat_events);
	bool block = true;
	struct tw_binding_node *keystate;
	struct tw_binding *binding = NULL;

	// grab quiting is done at release event, if key binding is executed or
	// we didn't hit a binding, it is about time to quit this grab. Note
	// that in we cannot do desc in cancel grab, as we may get into another
	// grab.
	if (!grab->data) {
		tw_keyboard_end_grab(&grab->seat->keyboard);
		return;
	} else if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}
	//this looks unintuitive, but we need essentially step first, since we
	//are in the root node when are first here.
	keystate = grab->data;
	keystate = tw_binding_node_step(keystate, key, mod_mask);
	binding = tw_binding_node_get_binding(keystate);
	if (binding) {
		keystate = NULL;
		//the key_function may get us into another grab.
		block = binding->key_func(&seat->keyboard, time_msec,
		                          key, mod_mask, binding->option,
		                          binding->user_data);
	}
	grab->data = keystate;
	//fall through.
	if (!block) {
		tw_keyboard_end_grab(&seat->keyboard);
		tw_keyboard_notify_key(&seat->keyboard, time_msec, key, state);
	}
}

static const struct tw_keyboard_grab_interface keybinding_impl = {
	.key = binding_key,
	.cancel = binding_key_cancel,
};

static void
binding_pointer_cancel(struct tw_seat_pointer_grab *grab)
{
	grab->data = NULL;
}


static void
binding_btn(struct tw_seat_pointer_grab *grab, uint32_t time, uint32_t button,
            enum wl_pointer_button_state state)
{
	struct tw_binding *binding = grab->data;
	struct tw_seat *seat = grab->seat;
	struct tw_seat_events *seat_events =
		container_of(grab, struct tw_seat_events,
		             binding_pointer_grab);
	uint32_t mod_mask = curr_modmask(seat_events);
	bool block;

	//binding button is a little different, since we do not have a state,
	//thus here we basically call the binding functions then leave at
	//release (next button). Unless we get into another grab.
	if (!binding || binding->type != TW_BINDING_btn ||
	    state != WL_POINTER_BUTTON_STATE_PRESSED) {
		tw_pointer_end_grab(&grab->seat->pointer);
		return;
	}
	block = binding->btn_func(&seat->pointer, time, button, mod_mask,
	                          binding->user_data);
	grab->data = NULL;
	if (!block) {
		tw_pointer_end_grab(&seat->pointer);
		tw_pointer_notify_button(&seat->pointer, time, button, state);
	}
}

static void
binding_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
             enum wl_pointer_axis orientation, double value,
             int32_t value_discrete,
             enum wl_pointer_axis_source source)
{

	struct tw_binding *binding = grab->data;
	struct tw_seat *seat = grab->seat;
	struct tw_seat_events *seat_events =
		container_of(grab, struct tw_seat_events,
		             binding_pointer_grab);
	uint32_t mod_mask = curr_modmask(seat_events);

        if (!binding || binding->type != TW_BINDING_axis) {
		tw_pointer_end_grab(&grab->seat->pointer);
		return;
	}
        binding->axis_func(&seat->pointer, time_msec, value, orientation,
                           mod_mask, binding->user_data);
	grab->data = NULL;
	//if the binding does not change the grab, we can actually quit the
	//binding grab.
	if (grab == seat->pointer.grab)
		tw_pointer_end_grab(&seat->pointer);
}

static const struct tw_pointer_grab_interface pointer_impl = {
	.button = binding_btn,
	.axis = binding_axis,
	.cancel = binding_pointer_cancel,
};

static void
binding_touch(struct tw_seat_touch_grab *grab, uint32_t time,
              uint32_t touch_id, double sx, double sy)
{
	struct tw_seat *seat = grab->seat;
	struct tw_binding *binding = grab->data;
	struct tw_seat_events *seat_events =
		container_of(grab, struct tw_seat_events,
		             binding_touch_grab);
	uint32_t mod_mask = curr_modmask(seat_events);

	if (!binding) {
		tw_touch_end_grab(&grab->seat->touch);
		return;
	}
	binding = grab->data;
	binding->touch_func(&seat->touch, time, mod_mask, binding->user_data);
	grab->data = NULL;
	if (grab == seat->touch.grab)
		tw_touch_end_grab(&seat->touch);
}

static void
binding_touch_cancel(struct tw_seat_touch_grab *grab)
{
	grab->data = NULL;
}

static const struct tw_touch_grab_interface touch_impl = {
	.down = binding_touch,
	.cancel = binding_touch_cancel,
};

/******************************************************************************
 * events
 *
 * The following listeners are the main input handlings in taiwins. It runs
 * before the backend input handling(which is calling the grab handlers) to
 * optionally setup the right grab.
 *
 * the handler in the backend would simply call the default grabs, which sends
 * the event to clients. However, there are many cases in the backend, it does
 * not know the complete information, especially if there are surface
 * cooridnates involved. But here we could actually have more information than
 * the backend, it maybe not necessary at all to handle input events in the
 * backend.
 *****************************************************************************/

static void
handle_key_input(struct wl_listener *listener, void *data)
{
	struct tw_binding_node *state;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, key_input);
	struct wlr_event_keyboard_key *event = data;
	struct tw_keyboard *seat_keyboard = &seat_events->seat->keyboard;

        if (seat_keyboard->grab != &seat_keyboard->default_grab ||
            event->state != WLR_KEY_PRESSED)
		return;
	state = tw_bindings_find_key(seat_events->bindings,
	                             event->keycode,
	                             curr_modmask(seat_events));
	if (state) {
		seat_events->binding_key_grab.data = state;
		tw_keyboard_start_grab(seat_keyboard,
		                       &seat_events->binding_key_grab);
	}
}

static void
handle_modifiers_input(struct wl_listener *listener, void *data)
{
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, mod_input);
	struct tw_seat *seat = seat_events->seat;
	struct tw_keyboard *keyboard = &seat->keyboard;

        //This is where we set the keyboard modifiers and leds.
	keyboard->modifiers_state = curr_modmask(seat_events);
	keyboard->led_state = curr_ledmask(seat_events);

}

static void
handle_btn_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, btn_input);
	struct wlr_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat_events->seat->pointer;

        if (seat_pointer->grab != &seat_pointer->default_grab ||
            event->state != WLR_BUTTON_PRESSED)
		return;
	binding = tw_bindings_find_btn(seat_events->bindings,
	                               event->button,
	                               curr_modmask(seat_events));
	if (binding) {
		seat_events->binding_pointer_grab.data = binding;
		tw_pointer_start_grab(seat_pointer,
		                      &seat_events->binding_pointer_grab);
	}
	//if we are to move part of backend code here, we simply have
	//seat_pointer->grab->impl->button();

}

static void
handle_axis_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, axis_input);
	struct tw_pointer *seat_pointer = &seat_events->seat->pointer;
	struct wlr_event_pointer_axis *event = data;
	enum wl_pointer_axis direction =
		(event->orientation == WLR_AXIS_ORIENTATION_HORIZONTAL) ?
		WL_POINTER_AXIS_HORIZONTAL_SCROLL :
		WL_POINTER_AXIS_VERTICAL_SCROLL;

        if (seat_pointer->grab != &seat_pointer->default_grab)
		return;
        binding = tw_bindings_find_axis(seat_events->bindings,
                                        direction, curr_modmask(seat_events));
        if (binding) {
	        seat_events->binding_pointer_grab.data = binding;
	        tw_pointer_start_grab(seat_pointer,
	                              &seat_events->binding_pointer_grab);
        }
}

static void
handle_touch_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, tch_input);
	struct tw_touch *seat_touch = &seat_events->seat->touch;

        if (seat_touch->grab != &seat_touch->default_grab)
		return;
	binding = tw_bindings_find_touch(seat_events->bindings,
	                                 curr_modmask(seat_events));
	if (binding) {
		seat_events->binding_touch_grab.data = binding;
		tw_touch_start_grab(seat_touch,
		                    &seat_events->binding_touch_grab);
	}
}

static void
handle_seat_change(struct wl_listener *listener, void *data)
{
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, seat_change);
	struct tw_backend_seat *seat = data;
	struct wlr_input_device *dev;

	if ((seat->capabilities & TW_INPUT_CAP_KEYBOARD) &&
	    !seat_events->keyboard_dev) {
		dev = seat->keyboard.device;
		seat_events->keyboard_dev = dev->keyboard;
		wl_signal_add(&dev->keyboard->events.key,
		              &seat_events->key_input);
		wl_signal_add(&dev->keyboard->events.modifiers,
		              &seat_events->mod_input);

	} else if (!(seat->capabilities & TW_INPUT_CAP_KEYBOARD) &&
	           seat_events->keyboard_dev) {
		seat_events->keyboard_dev = NULL;
		tw_reset_wl_list(&seat_events->key_input.link);
		tw_reset_wl_list(&seat_events->mod_input.link);
	}

	if ((seat->capabilities & TW_INPUT_CAP_POINTER) &&
	    !(seat_events->pointer_dev)) {
		dev = seat->pointer.device;
		seat_events->pointer_dev = dev->pointer;
		wl_signal_add(&dev->pointer->events.button,
		              &seat_events->btn_input);
		wl_signal_add(&dev->pointer->events.axis,
		              &seat_events->axis_input);

	} else if (!(seat->capabilities & TW_INPUT_CAP_POINTER) &&
	           seat_events->pointer_dev) {
		seat_events->pointer_dev = NULL;
		tw_reset_wl_list(&seat_events->btn_input.link);
		tw_reset_wl_list(&seat_events->axis_input.link);
	}

	if ((seat->capabilities & TW_INPUT_CAP_TOUCH) &&
	    !seat_events->touch_dev) {
		dev = seat->touch.device;
		seat_events->touch_dev = dev->touch;
		wl_signal_add(&dev->touch->events.down,
		              &seat_events->tch_input);
	} else if (!(seat->capabilities & TW_INPUT_CAP_TOUCH) &&
	           seat_events->touch_dev) {
		seat_events->touch_dev = NULL;
		tw_reset_wl_list(&seat_events->tch_input.link);
	}
}

void
tw_seat_events_init(struct tw_seat_events *seat_events,
                    struct tw_backend_seat *seat, struct tw_bindings *bindings)
{
	seat_events->seat = seat->tw_seat;
	seat_events->bindings = bindings;
	seat_events->keyboard_dev = NULL;
	seat_events->pointer_dev = NULL;
	seat_events->touch_dev = NULL;

	//setup listeners
	wl_list_init(&seat_events->key_input.link);
	seat_events->key_input.notify = handle_key_input;
	wl_list_init(&seat_events->mod_input.link);
	seat_events->mod_input.notify = handle_modifiers_input;
	seat_events->binding_key_grab.impl = &keybinding_impl;
	seat_events->binding_key_grab.data = NULL;

	wl_list_init(&seat_events->btn_input.link);
	seat_events->btn_input.notify = handle_btn_input;
	wl_list_init(&seat_events->axis_input.link);
	seat_events->axis_input.notify = handle_axis_input;
	seat_events->binding_pointer_grab.impl = &pointer_impl;
	seat_events->binding_pointer_grab.data = NULL;

	wl_list_init(&seat_events->tch_input.link);
	seat_events->tch_input.notify = handle_touch_input;
	seat_events->binding_touch_grab.impl = &touch_impl;
	seat_events->binding_touch_grab.data = NULL;

	wl_list_init(&seat_events->seat_change.link);
	seat_events->seat_change.notify = handle_seat_change;
	wl_signal_add(&seat->backend->seat_ch_signal,
	              &seat_events->seat_change);
}

void
tw_seat_events_fini(struct tw_seat_events *seat_events)
{
	wl_list_remove(&seat_events->seat_change.link);
	wl_list_remove(&seat_events->key_input.link);
	wl_list_remove(&seat_events->mod_input.link);
	wl_list_remove(&seat_events->btn_input.link);
	wl_list_remove(&seat_events->axis_input.link);
	wl_list_remove(&seat_events->tch_input.link);
}
