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
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/virtual_keyboard.h>
#include <taiwins/shell.h>

#include <taiwins/engine.h>
#include <taiwins/backend.h>
#include <taiwins/input_device.h>
#include <taiwins/login.h>
#include "desktop/xdg.h"

#include "bindings.h"
#include "config/config.h"
#include "input.h"

#define TW_SESSION_SWITCH_GRAB_LEVEL	0xffffffff
#define TW_BINDING_GRAB_ORDER		TW_XDG_GRAB_ORDER

/******************************************************************************
 * bindings
 *
 * Binding grab used in taiwins. The grabs do the search for binding and
 * calling the corresponding binding if found.
 *
 *****************************************************************************/
static void
binding_key_cancel(struct tw_seat_keyboard_grab *grab)
{
	grab->data = NULL;
}

static void
binding_key_pop(struct tw_seat_keyboard_grab *grab,
                enum tw_seat_grab_action action)
{
	//coming from grab stack popping, we just remove the grab here
	if (action == TW_SEAT_GRAB_POP)
		tw_keyboard_end_grab(&grab->seat->keyboard, grab);
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
		tw_keyboard_end_grab(&grab->seat->keyboard, grab);
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
		tw_keyboard_end_grab(&seat->keyboard, grab);
		tw_keyboard_notify_key(&seat->keyboard, time_msec, key, state);
	}
}

static const struct tw_keyboard_grab_interface keybinding_impl = {
	.enter = tw_keyboard_default_enter,
	.key = binding_key,
	.cancel = binding_key_cancel,
	.grab_action = binding_key_pop,
};

static void
binding_pointer_cancel(struct tw_seat_pointer_grab *grab)
{
	grab->data = NULL;
}

static void
binding_pointer_pop(struct tw_seat_pointer_grab *grab,
                    enum tw_seat_grab_action action)
{
	//coming from grab stack popping, we just remove the grab here
	if (action == TW_SEAT_GRAB_POP)
		tw_pointer_end_grab(&grab->seat->pointer, grab);
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
		tw_pointer_end_grab(&grab->seat->pointer, grab);
		return;
	}
	//we could also get into another grab
	block = binding->btn_func(&seat->pointer, time, button, mod_mask,
	                          binding->user_data);
	grab->data = NULL;
	if (!block) {
		tw_pointer_end_grab(&seat->pointer, grab);
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
		tw_pointer_end_grab(&grab->seat->pointer, grab);
		return;
	}
	binding->axis_func(&seat->pointer, time_msec, value, orientation,
	                   mod_mask, binding->user_data);
	grab->data = NULL;
	//if the binding does not change the grab, we can actually quit the
	//binding grab.
	if (grab == seat->pointer.grab)
		tw_pointer_end_grab(&seat->pointer, grab);
}

static const struct tw_pointer_grab_interface pointer_impl = {
	.enter = tw_pointer_default_enter,
	.button = binding_btn,
	.axis = binding_axis,
	.cancel = binding_pointer_cancel,
	.grab_action = binding_pointer_pop,
};

static void
binding_touch_cancel(struct tw_seat_touch_grab *grab)
{
	grab->data = NULL;
}

static void
binding_touch_pop(struct tw_seat_touch_grab *grab,
                  enum tw_seat_grab_action action)
{
	if (action == TW_SEAT_GRAB_POP)
		tw_touch_end_grab(&grab->seat->touch, grab);
}

static void
binding_touch(struct tw_seat_touch_grab *grab, uint32_t time,
              uint32_t touch_id, double sx, double sy)
{
	struct tw_seat *seat = grab->seat;
	struct tw_binding *binding = grab->data;
	uint32_t mod_mask = seat->keyboard.modifiers_state;

	if (!binding) {
		tw_touch_end_grab(&grab->seat->touch, grab);
		return;
	}
	binding = grab->data;
	binding->touch_func(&seat->touch, time, mod_mask, binding->user_data);
	grab->data = NULL;
	if (grab == seat->touch.grab)
		tw_touch_end_grab(&seat->touch, grab);
}

static const struct tw_touch_grab_interface touch_impl = {
	.enter = tw_touch_default_enter,
	.down = binding_touch,
	.cancel = binding_touch_cancel,
	.grab_action = binding_touch_pop,
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
	struct tw_seat_listeners *seat_listeners =
		wl_container_of(grab, seat_listeners, session_switch_grab);
	struct tw_backend *backend = seat_listeners->engine->backend;
	struct tw_login *login = tw_backend_get_login(backend);
	int sid = *(int *)grab->data;

	if (login) {
		tw_login_switch_vt(login, sid);
		tw_keyboard_end_grab(&grab->seat->keyboard, grab);
	}
}

static const struct tw_keyboard_grab_interface session_switch_impl = {
	.key = session_switch_key,
};

/******************************************************************************
 * virtual keyboard
 *****************************************************************************/

struct tw_vkeyboard_device {
	struct tw_input_device dev;
	struct tw_virtual_keyboard *keyboard;
	struct wl_listener destroy_listener;
};

static void
handle_destroy_vkeyboard(struct tw_input_device *device)
{
	struct tw_vkeyboard_device *dev =
		wl_container_of(device, dev, dev);
	dev->keyboard = NULL;
	wl_list_remove(&dev->destroy_listener.link);
	free(dev);
}

static const struct tw_input_device_impl vkeyboard_impl = {
	.destroy = handle_destroy_vkeyboard,
};

static void
notify_vkeyboard_destroy(struct wl_listener *listener, void *data)
{
	struct tw_vkeyboard_device *dev =
		wl_container_of(listener, dev, destroy_listener);
	//listener removed in handle_destroy_vkeyboard
	tw_input_device_fini(&dev->dev);
}

static struct tw_input_device *
tw_server_create_virtual_keyboard_device(struct tw_virtual_keyboard *keyboard,
                                         struct tw_engine_seat *seat)
{
	struct tw_vkeyboard_device *dev = NULL;

	assert(seat->tw_seat == keyboard->seat);
	if (!(dev = calloc(1, sizeof(*dev))))
		return NULL;
	dev->keyboard = keyboard;
	dev->dev.vendor = 0xabcd;
	dev->dev.product = 0x1234;
	strncpy(dev->dev.name, "virtual-keyboard", sizeof(dev->dev.name));

	tw_input_device_init(&dev->dev, TW_INPUT_TYPE_KEYBOARD, seat->idx,
	                     &vkeyboard_impl);

	tw_signal_setup_listener(&keyboard->destroy_signal,
	                         &dev->destroy_listener,
	                         notify_vkeyboard_destroy);
	return &dev->dev;
}


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
	struct tw_seat_listeners *seat_listeners =
		wl_container_of(listener, seat_listeners, key_input);

	struct tw_event_keyboard_key *event = data;
	struct tw_keyboard *seat_keyboard = &seat_listeners->seat->keyboard;

	seat_listeners->curr_state = event->dev->input.keyboard.keystate;
	if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if ((sid = session_switch_get_idx(event->keycode, event->dev)) >= 0) {
		seat_listeners->session_switch_grab.data = &sid;
		tw_keyboard_start_grab(seat_keyboard,
		                       &seat_listeners->session_switch_grab,
		                       TW_SESSION_SWITCH_GRAB_LEVEL);
		return;
	}

	state = tw_bindings_find_key(seat_listeners->bindings,
	                             event->keycode,
	                             seat_keyboard->modifiers_state);
	if (state) {
		seat_listeners->binding_key_grab.data = state;
		tw_keyboard_start_grab(seat_keyboard,
		                       &seat_listeners->binding_key_grab,
		                       TW_BINDING_GRAB_ORDER);
	}
}

static void
notify_btn_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_listeners *seat_listeners =
		wl_container_of(listener, seat_listeners, btn_input);
	struct tw_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat_listeners->seat->pointer;
	struct tw_keyboard *seat_keyboard = &seat_listeners->seat->keyboard;

	if (seat_pointer->grab != &seat_pointer->default_grab ||
	    event->state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	binding = tw_bindings_find_btn(seat_listeners->bindings,
	                               event->button,
	                               seat_keyboard->modifiers_state);
	if (binding) {
		seat_listeners->binding_pointer_grab.data = binding;
		tw_pointer_start_grab(seat_pointer,
		                      &seat_listeners->binding_pointer_grab,
		                      TW_BINDING_GRAB_ORDER);
	}
	//if we are to move part of backend code here, we simply have
	//seat_pointer->grab->impl->button();
}

static void
notify_axis_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_listeners *seat_listeners =
		wl_container_of(listener, seat_listeners, axis_input);
	struct tw_pointer *seat_pointer = &seat_listeners->seat->pointer;
	struct tw_keyboard *seat_keyboard = &seat_listeners->seat->keyboard;

	struct tw_event_pointer_axis *event = data;

	if (seat_pointer->grab != &seat_pointer->default_grab)
		return;
	binding = tw_bindings_find_axis(seat_listeners->bindings,
	                                event->axis,
	                                seat_keyboard->modifiers_state);
	if (binding) {
		seat_listeners->binding_pointer_grab.data = binding;
		tw_pointer_start_grab(seat_pointer,
		                      &seat_listeners->binding_pointer_grab,
		                      TW_BINDING_GRAB_ORDER);
	}
}

static void
notify_touch_input(struct wl_listener *listener, void *data)
{
	struct tw_binding *binding;
	struct tw_seat_listeners *seat_listeners =
		wl_container_of(listener, seat_listeners, tch_input);
	struct tw_touch *seat_touch = &seat_listeners->seat->touch;
	struct tw_keyboard *seat_keyboard = &seat_listeners->seat->keyboard;

	if (seat_touch->grab != &seat_touch->default_grab)
		return;
	binding = tw_bindings_find_touch(seat_listeners->bindings,
	                                 seat_keyboard->modifiers_state);
	if (binding) {
		seat_listeners->binding_touch_grab.data = binding;
		tw_touch_start_grab(seat_touch,
		                    &seat_listeners->binding_touch_grab,
		                    TW_BINDING_GRAB_ORDER);
	}
}

static void
tw_seat_listeners_init(struct tw_seat_listeners *seat_listeners,
                       struct tw_engine_seat *seat,
                       struct tw_bindings *bindings)
{
	seat_listeners->seat = seat->tw_seat;
	seat_listeners->engine = seat->engine;
	seat_listeners->bindings = bindings;

	//setup listeners
	tw_signal_setup_listener(&seat->source.keyboard.key,
	                         &seat_listeners->key_input,
	                         notify_key_input);
	tw_signal_setup_listener(&seat->source.pointer.button,
	                         &seat_listeners->btn_input,
	                         notify_btn_input);
	tw_signal_setup_listener(&seat->source.pointer.axis,
	                         &seat_listeners->axis_input,
	                         notify_axis_input);
	tw_signal_setup_listener(&seat->source.touch.down,
	                         &seat_listeners->tch_input,
	                         notify_touch_input);
	//grabs
	seat_listeners->binding_key_grab.impl = &keybinding_impl;
	seat_listeners->binding_key_grab.data = NULL;

	seat_listeners->session_switch_grab.impl = &session_switch_impl;
	seat_listeners->session_switch_grab.data = NULL;

	seat_listeners->binding_pointer_grab.impl = &pointer_impl;
	seat_listeners->binding_pointer_grab.data = NULL;

	seat_listeners->binding_touch_grab.impl = &touch_impl;
	seat_listeners->binding_touch_grab.data = NULL;
}

static void
tw_seat_listeners_fini(struct tw_seat_listeners *seat_listeners)
{
	wl_list_remove(&seat_listeners->key_input.link);
	wl_list_remove(&seat_listeners->btn_input.link);
	wl_list_remove(&seat_listeners->axis_input.link);
	wl_list_remove(&seat_listeners->tch_input.link);
}

static void
notify_adding_seat(struct wl_listener *listener, void *data)
{
	struct tw_server_input_manager *mgr =
		wl_container_of(listener, mgr, listeners.seat_add);
	struct tw_engine_seat *seat = data;

	tw_seat_listeners_init(&mgr->inputs[seat->idx],
	                       seat, &mgr->config->config_table.bindings);
}

static void
notify_removing_seat(struct wl_listener *listener, void *data)
{
	struct tw_server_input_manager *mgr =
		wl_container_of(listener, mgr, listeners.seat_remove);
	struct tw_engine_seat *seat = data;

	tw_seat_listeners_fini(&mgr->inputs[seat->idx]);
}

static int
notify_input_idle(void *data)
{
	struct tw_server_input_manager *mgr = data;
	struct tw_shell *shell = tw_config_request_object(mgr->config,"shell");

	if (shell)
		tw_shell_start_locker(shell);
	return 0;
}

static void
notify_input_manager_seat_input(struct wl_listener *listener, void *data)
{
	struct tw_server_input_manager *mgr =
		wl_container_of(listener, mgr, listeners.seat_input);
	uint32_t idle_time = mgr->config->config_table.lock_timer.val;

	if (mgr->idle_timer) {
		wl_event_source_timer_update(mgr->idle_timer, idle_time*1000);
	} else {
		tw_logl_level(TW_LOG_WARN, "idle timer not available");
	}
}

static void
notify_input_manager_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_server_input_manager *mgr =
		wl_container_of(listener, mgr, listeners.display_destroy);
}

static void
notify_adding_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct tw_server_input_manager *mgr =
		wl_container_of(listener, mgr, listeners.new_virtual_keyboard);
	struct tw_virtual_keyboard *keyboard = data;
	struct tw_engine *engine = mgr->engine;
	struct tw_backend *backend = engine->backend;
	struct tw_engine_seat *seat =
		tw_engine_seat_from_seat(engine, keyboard->seat);
	struct tw_input_device *device =
		tw_server_create_virtual_keyboard_device(data, seat);

	if (device) {
		wl_list_insert(backend->inputs.prev, &device->link);
		wl_signal_emit(&backend->signals.new_input, device);
	}
}

struct tw_server_input_manager *
tw_server_input_manager_create_global(struct tw_engine *engine,
                                      struct tw_config *config)
{
	static struct tw_server_input_manager mgr = {0};
	struct wl_event_loop *lo =
		wl_display_get_event_loop(engine->display);

	mgr.engine = engine;
	mgr.config = config;
	mgr.idle_timer = wl_event_loop_add_timer(lo, notify_input_idle, &mgr);

	tw_virtual_keyboard_manager_init(&mgr.vkeyboard_mgr, engine->display);
	tw_input_method_manager_init(&mgr.im_mgr, engine->display);
	tw_text_input_manager_init(&mgr.ti_mgr, engine->display);

	tw_signal_setup_listener(&engine->signals.seat_created,
	                         &mgr.listeners.seat_add,
	                         notify_adding_seat);
	tw_signal_setup_listener(&engine->signals.seat_remove,
	                         &mgr.listeners.seat_remove,
	                         notify_removing_seat);

	tw_signal_setup_listener(&mgr.vkeyboard_mgr.new_keyboard,
	                         &mgr.listeners.new_virtual_keyboard,
	                         notify_adding_virtual_keyboard);
	tw_signal_setup_listener(&engine->signals.seat_input,
	                         &mgr.listeners.seat_input,
	                         notify_input_manager_seat_input);

	tw_set_display_destroy_listener(engine->display,
	                                &mgr.listeners.display_destroy,
	                                notify_input_manager_display_destroy);

	return &mgr;
}
