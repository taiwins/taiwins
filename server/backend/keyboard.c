/*
 * keyboard.c - taiwins backend keyboard functions
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
