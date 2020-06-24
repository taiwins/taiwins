/*
 * backend_seat.c - taiwins backend seat functions
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

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <xkbcommon/xkbcommon-compat.h>
#include <xkbcommon/xkbcommon.h>
#include <ctypes/helpers.h>
#include <objects/seat.h>

#include "backend.h"
#include "backend_internal.h"

/******************************************************************************
 * keyboard functions
 *****************************************************************************/

static void
notify_backend_keyboard_remove(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             keyboard.destroy);

	//uninstall the listeners
	wl_list_remove(&seat->keyboard.destroy.link);
	wl_list_remove(&seat->keyboard.key.link);
	//wl_list_remove(&seat->keyboard.keymap.link);
	wl_list_remove(&seat->keyboard.modifiers.link);

	//update the capabilities
	seat->capabilities &= ~TW_INPUT_CAP_KEYBOARD;
	tw_seat_remove_keyboard(seat->tw_seat);
	//update the signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	if (seat->capabilities == 0)
		tw_backend_seat_destroy(seat);
}

static void
notify_backend_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             keyboard.modifiers);
	struct wlr_keyboard *keyboard = data;
	struct tw_keyboard *seat_keyboard = &seat->tw_seat->keyboard;
	uint32_t depressed = keyboard->modifiers.depressed;
	uint32_t latched = keyboard->modifiers.latched;
	uint32_t locked = keyboard->modifiers.locked;
	uint32_t group = keyboard->modifiers.group;

        if (seat_keyboard->grab->impl->modifiers)
	        seat_keyboard->grab->impl->modifiers(seat_keyboard->grab,
	                                             depressed,
	                                             latched,
	                                             locked,
	                                             group);
}

/* the noifiers here are the last to run. there are other notifiers being run
 * before this */
static void
notify_backend_keyboard_key(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             keyboard.key);
	struct wlr_event_keyboard_key *event = data;
	struct tw_keyboard *seat_keyboard = &seat->tw_seat->keyboard;
	uint32_t state = event->state == WLR_KEY_PRESSED ?
		WL_KEYBOARD_KEY_STATE_PRESSED :
		WL_KEYBOARD_KEY_STATE_RELEASED;

        if (seat_keyboard->grab->impl->key)
		seat_keyboard->grab->impl->key(seat_keyboard->grab,
		                               event->time_msec,
		                               event->keycode,
		                               state);
}

static void
notify_backend_keyboard_keymap(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             keyboard.keymap);
	struct tw_keyboard *seat_keyboard = &seat->tw_seat->keyboard;
	struct wlr_keyboard *wlr_keyboard = data;

	tw_keyboard_set_keymap(seat_keyboard, wlr_keyboard->keymap);
}

void
tw_backend_new_keyboard(struct tw_backend *backend,
                        struct wlr_input_device *dev)
{
	struct xkb_rule_names rules = {0};
	struct xkb_keymap *keymap;
	struct tw_backend_seat *seat =
		tw_backend_seat_find_create(backend, dev,
		                            TW_INPUT_CAP_KEYBOARD);
	if (!seat) return;
	//xkbcommon settings
	seat->keyboard.device = dev;
	keymap = xkb_map_new_from_names(backend->xkb_context,
	                                &rules,
	                                XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(dev->keyboard, keymap);
	wlr_keyboard_set_repeat_info(dev->keyboard, 25, 600);
	xkb_keymap_unref(keymap);

	//update the capabilities
	seat->capabilities |= TW_INPUT_CAP_KEYBOARD;
	tw_seat_new_keyboard(seat->tw_seat);
	tw_keyboard_set_keymap(&seat->tw_seat->keyboard, keymap);
	//update the signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	//TODO listeners are installed at last here to give any user who listen
	//to the seat_ch_signal

	//install listeners
	wl_list_init(&seat->keyboard.destroy.link);
	seat->keyboard.destroy.notify = notify_backend_keyboard_remove;
	wl_signal_add(&dev->keyboard->events.destroy,
	              &seat->keyboard.destroy);
	wl_list_init(&seat->keyboard.modifiers.link);
	seat->keyboard.modifiers.notify = notify_backend_keyboard_modifiers;
	wl_signal_add(&dev->keyboard->events.modifiers,
	              &seat->keyboard.modifiers);
	wl_list_init(&seat->keyboard.key.link);
	seat->keyboard.key.notify = notify_backend_keyboard_key;
	wl_signal_add(&dev->keyboard->events.key,
	              &seat->keyboard.key);
	wl_list_init(&seat->keyboard.keymap.link);
	seat->keyboard.keymap.notify = notify_backend_keyboard_keymap;
	wl_signal_add(&dev->keyboard->events.keymap, &seat->keyboard.keymap);
}

/******************************************************************************
 * pointer functions
 *****************************************************************************/

static void
notify_backend_pointer_button(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.button);
	struct wlr_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;
	uint32_t state = event->state == WLR_BUTTON_PRESSED ?
		WL_POINTER_BUTTON_STATE_PRESSED :
		WL_POINTER_BUTTON_STATE_RELEASED;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		seat_pointer->btn_count++;
	else
		seat_pointer->btn_count--;

	if (seat_pointer->grab->impl->button)
		seat_pointer->grab->impl->button(seat_pointer->grab,
		                                 event->time_msec,
		                                 event->button,
		                                 state);
}

static void
notify_backend_pointer_motion(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.motion);
	struct wlr_event_pointer_motion *event = data;
	struct tw_backend *backend = seat->backend;
	struct wlr_cursor *cursor = backend->global_cursor;

	//we have only relative motion now.
        cursor->x += event->delta_x;
        cursor->y += event->delta_y;

        //obviously we do not have any information about surface at this point.
        wl_signal_emit(&cursor->events.motion, data);
}

static void
notify_backend_pointer_axis(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.axis);
	struct wlr_event_pointer_axis *event = data;
	struct tw_backend *backend = seat->backend;
	struct wlr_cursor *cursor = backend->global_cursor;

	//TODO: All we can do is forwarding the event for now.
	wl_signal_emit(&cursor->events.axis, event);
}

static void
notify_backend_pointer_frame(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.frame);
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;
	if (seat_pointer->grab->impl->frame)
		seat_pointer->grab->impl->frame(seat_pointer->grab);
}

static void
notify_backend_pointer_remove(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.destroy);

	wl_list_remove(&seat->pointer.destroy.link);
	//update the capabilities
	seat->capabilities &= ~TW_INPUT_CAP_POINTER;
	tw_seat_remove_pointer(seat->tw_seat);
	//update the signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);
	if (seat->capabilities == 0)
		tw_backend_seat_destroy(seat);
}

void
tw_backend_new_pointer(struct tw_backend *backend,
                       struct wlr_input_device *dev)
{
	struct wlr_pointer *pointer = dev->pointer;
	struct tw_backend_seat *seat =
		tw_backend_seat_find_create(backend, dev,
		                            TW_INPUT_CAP_POINTER);
	if (!seat) return;
	seat->pointer.device = dev;
        //update the capabilities
	seat->capabilities |= TW_INPUT_CAP_POINTER;
	tw_seat_new_pointer(seat->tw_seat);
	//update the signals earlier than listener to have a
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	//add listeners
	wl_list_init(&seat->pointer.destroy.link);
	seat->pointer.destroy.notify = notify_backend_pointer_remove;
	wl_signal_add(&dev->events.destroy, &seat->pointer.destroy);

	wl_list_init(&seat->pointer.button.link);
	seat->pointer.button.notify = notify_backend_pointer_button;
	wl_signal_add(&pointer->events.button, &seat->pointer.button);

	wl_list_init(&seat->pointer.motion.link);
	seat->pointer.motion.notify = notify_backend_pointer_motion;
	wl_signal_add(&pointer->events.motion, &seat->pointer.motion);

	wl_list_init(&seat->pointer.axis.link);
	seat->pointer.axis.notify = notify_backend_pointer_axis;
	wl_signal_add(&pointer->events.axis, &seat->pointer.axis);

        wl_list_init(&seat->pointer.frame.link);
	seat->pointer.frame.notify = notify_backend_pointer_frame;
	wl_signal_add(&pointer->events.frame, &seat->pointer.frame);
}

/******************************************************************************
 * touch functions
 *****************************************************************************/

static void
notify_backend_touch_remove(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.destroy);
	wl_list_remove(&seat->touch.destroy.link);
	//update the capabilities
	seat->capabilities &= ~TW_INPUT_CAP_TOUCH;
	tw_seat_remove_touch(seat->tw_seat);
	//update signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);
	if (seat->capabilities == 0)
		tw_backend_seat_destroy(seat);
}

static void
notify_backend_touch_down(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.down);
	struct wlr_event_touch_down *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_up(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.up);
	struct wlr_event_touch_up *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_motion(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.motion);
	struct wlr_event_touch_motion *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_cancel(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.cancel);
	struct wlr_event_touch_cancel *event = data;
	(void)seat; (void)event;
}

void
tw_backend_new_touch(struct tw_backend *backend,
                     struct wlr_input_device *dev)
{
	struct tw_backend_seat *seat =
		tw_backend_seat_find_create(backend, dev,
		                            TW_INPUT_CAP_TOUCH);
	if (!seat) return;

	seat->touch.device = dev;
	//update capabilities
	seat->capabilities |= TW_INPUT_CAP_TOUCH;
	tw_seat_new_touch(seat->tw_seat);
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	//install listeners
	wl_list_init(&seat->touch.destroy.link);
	seat->touch.destroy.notify = notify_backend_touch_remove;
	wl_signal_add(&dev->events.destroy, &seat->touch.destroy);

	wl_list_init(&seat->touch.down.link);
	seat->touch.down.notify = notify_backend_touch_down;
	wl_signal_add(&dev->touch->events.down, &seat->touch.down);

	wl_list_init(&seat->touch.up.link);
	seat->touch.up.notify = notify_backend_touch_up;
	wl_signal_add(&dev->touch->events.up, &seat->touch.up);

	wl_list_init(&seat->touch.motion.link);
	seat->touch.motion.notify = notify_backend_touch_motion;
	wl_signal_add(&dev->touch->events.motion, &seat->touch.motion);

	wl_list_init(&seat->touch.cancel.link);
	seat->touch.cancel.notify = notify_backend_touch_cancel;
	wl_signal_add(&dev->touch->events.cancel, &seat->touch.cancel);
}

/******************************************************************************
 * seat functions
 *****************************************************************************/

static struct tw_backend_seat *
find_seat_missing_dev(struct tw_backend *backend,
                      struct wlr_input_device *dev,
                      enum tw_input_device_cap cap)
{
	struct tw_backend_seat *seat;

	wl_list_for_each(seat, &backend->inputs, link) {
		if (!(seat->capabilities & cap))
			return seat;
	}

	return NULL;
}

static struct tw_backend_seat *
new_seat_for_backend(struct tw_backend *backend,
                     struct wlr_input_device *dev)
{
	struct tw_backend_seat *seat;
	int new_seat_id = ffs(backend->seat_pool);
	if (new_seat_id >= 8)
		return NULL;

	// init the seat
	seat = &backend->seats[new_seat_id];
	seat->backend = backend;
	seat->idx = new_seat_id;
	seat->capabilities = 0;
	seat->tw_seat = tw_seat_create(backend->display, dev->name);

	wl_list_init(&seat->link);
	// setup the backend side
	backend->seat_pool |= (1 << new_seat_id);
	wl_list_insert(backend->inputs.prev, &seat->link);
	return seat;
}

struct tw_backend_seat *
tw_backend_seat_find_create(struct tw_backend *backend,
                            struct wlr_input_device *dev,
                            enum tw_input_device_cap cap)
{
	struct tw_backend_seat *seat =
		find_seat_missing_dev(backend, dev, cap);
	if (!seat) {
		seat = new_seat_for_backend(backend, dev);
		wl_signal_emit(&backend->seat_add_signal, seat);
	}
	if (!seat)
		return NULL;

	return seat;
}

void
tw_backend_seat_destroy(struct tw_backend_seat *seat)
{
	uint32_t unset = ~(1 << seat->idx);

	wl_signal_emit(&seat->backend->seat_rm_signal, seat);

	wl_list_remove(&seat->link);
	seat->idx = -1;
	tw_seat_destroy(seat->tw_seat);
	seat->tw_seat = NULL;

	seat->backend->seat_pool &= unset;
}

void *
tw_backend_seat_get_backend(struct tw_backend_seat *seat)
{
	return seat->tw_seat;
}

void
tw_backend_seat_set_xkb_rules(struct tw_backend_seat *seat,
                              struct xkb_rule_names *rules)
{
	//TODO
}
