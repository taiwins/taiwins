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
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>

#include <taiwins/engine.h>
#include <taiwins/input_device.h>
#include "bindings.h"
#include "input.h"

/******************************************************************************
 * bindings
 *
 * Binding grab used in taiwins. The grabs do the search for binding and calling
 * the corresponding binding if found.
 *
 *****************************************************************************/
static void
binding_key_cancel(struct tw_seat_keyboard_grab *grab)
{
	grab->data = NULL;
}

static void
binding_key(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
            uint32_t key, uint32_t state)
{
	struct tw_seat *seat = grab->seat;
	uint32_t mod_mask = seat->keyboard.modifiers_state;
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
	uint32_t mod_mask = seat->keyboard.modifiers_state;
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
	uint32_t mod_mask = seat->keyboard.modifiers_state;

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
	uint32_t mod_mask = seat->keyboard.modifiers_state;

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
 * session_switch grab
 *****************************************************************************/
static int
session_switch_get_idx(uint32_t key, struct tw_input_device *dev)
{
	const xkb_keysym_t *keysyms;
	uint32_t nsyms = xkb_state_key_get_syms(dev->input.keyboard.keystate,
	                                        key+8, &keysyms);

	for (unsigned i = 0; i < nsyms; i++) {
		xkb_keysym_t keysym = keysyms[i];
		if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
		    keysym <= XKB_KEY_XF86Switch_VT_12) {
			return keysym - XKB_KEY_XF86Switch_VT_1+1;
		}
	}
	return -1;
}

static void
session_switch_key(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
                   uint32_t key, uint32_t state)
{
	struct tw_seat_events *seat_events =
		wl_container_of(grab, seat_events, session_switch_grab);
	int sid = *(int *)grab->data;
	(void)sid;

	/* struct tw_engine *engine = seat_events->engine; */
	/* tw_backend_switch_session(backend, sid); */
}

static const struct tw_keyboard_grab_interface session_switch_impl = {
	.key = session_switch_key,
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
notify_key_input(struct wl_listener *listener, void *data)
{
	static int sid = -1;
	struct tw_binding_node *state;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, key_input);

	struct tw_event_keyboard_key *event = data;
	struct tw_keyboard *seat_keyboard = &seat_events->seat->keyboard;

	seat_events->curr_state = event->dev->input.keyboard.keystate;
        if (seat_keyboard->grab != &seat_keyboard->default_grab ||
            event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

        if ((sid = session_switch_get_idx(event->keycode, event->dev)) >= 0) {
	        seat_events->session_switch_grab.data = &sid;
	        tw_keyboard_start_grab(seat_keyboard,
	                               &seat_events->session_switch_grab);
	        return;
        }

	state = tw_bindings_find_key(seat_events->bindings,
	                             event->keycode,
	                             seat_keyboard->modifiers_state);
	if (state) {
		seat_events->binding_key_grab.data = state;
		tw_keyboard_start_grab(seat_keyboard,
		                       &seat_events->binding_key_grab);
	}
}

static void
notify_btn_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, btn_input);
	struct tw_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat_events->seat->pointer;
	struct tw_keyboard *seat_keyboard = &seat_events->seat->keyboard;

        if (seat_pointer->grab != &seat_pointer->default_grab ||
            event->state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	binding = tw_bindings_find_btn(seat_events->bindings,
	                               event->button,
	                               seat_keyboard->modifiers_state);
	if (binding) {
		seat_events->binding_pointer_grab.data = binding;
		tw_pointer_start_grab(seat_pointer,
		                      &seat_events->binding_pointer_grab);
	}
	//if we are to move part of backend code here, we simply have
	//seat_pointer->grab->impl->button();
}

static void
notify_axis_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, axis_input);
	struct tw_pointer *seat_pointer = &seat_events->seat->pointer;
	struct tw_keyboard *seat_keyboard = &seat_events->seat->keyboard;

	struct tw_event_pointer_axis *event = data;

        if (seat_pointer->grab != &seat_pointer->default_grab)
		return;
        binding = tw_bindings_find_axis(seat_events->bindings,
                                        event->axis,
                                        seat_keyboard->modifiers_state);
        if (binding) {
	        seat_events->binding_pointer_grab.data = binding;
	        tw_pointer_start_grab(seat_pointer,
	                              &seat_events->binding_pointer_grab);
        }
}

static void
notify_touch_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_events *seat_events =
		container_of(listener, struct tw_seat_events, tch_input);
	struct tw_touch *seat_touch = &seat_events->seat->touch;
	struct tw_keyboard *seat_keyboard = &seat_events->seat->keyboard;

        if (seat_touch->grab != &seat_touch->default_grab)
		return;
	binding = tw_bindings_find_touch(seat_events->bindings,
	                                 seat_keyboard->modifiers_state);
	if (binding) {
		seat_events->binding_touch_grab.data = binding;
		tw_touch_start_grab(seat_touch,
		                    &seat_events->binding_touch_grab);
	}
}

void
tw_seat_events_init(struct tw_seat_events *seat_events,
                    struct tw_engine_seat *seat, struct tw_bindings *bindings)
{
	seat_events->seat = seat->tw_seat;
	seat_events->engine = seat->engine;
	seat_events->bindings = bindings;

	//setup listeners
	tw_signal_setup_listener(&seat->source.keyboard.key,
	                         &seat_events->key_input,
	                         notify_key_input);
	tw_signal_setup_listener(&seat->source.pointer.button,
	                         &seat_events->btn_input,
	                         notify_btn_input);
	tw_signal_setup_listener(&seat->source.pointer.axis,
	                         &seat_events->axis_input,
	                         notify_axis_input);
	tw_signal_setup_listener(&seat->source.touch.down,
	                         &seat_events->tch_input,
	                         notify_touch_input);
	//grabs
	seat_events->binding_key_grab.impl = &keybinding_impl;
	seat_events->binding_key_grab.data = NULL;

	seat_events->session_switch_grab.impl = &session_switch_impl;
	seat_events->session_switch_grab.data = NULL;

	seat_events->binding_pointer_grab.impl = &pointer_impl;
	seat_events->binding_pointer_grab.data = NULL;

	seat_events->binding_touch_grab.impl = &touch_impl;
	seat_events->binding_touch_grab.data = NULL;
}

void
tw_seat_events_fini(struct tw_seat_events *seat_events)
{
	wl_list_remove(&seat_events->key_input.link);
	wl_list_remove(&seat_events->btn_input.link);
	wl_list_remove(&seat_events->axis_input.link);
	wl_list_remove(&seat_events->tch_input.link);
}
