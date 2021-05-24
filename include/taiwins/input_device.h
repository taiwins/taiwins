/*
 * input_device.h - taiwins server input device header
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

#ifndef TW_INPUT_DEVICE_H
#define TW_INPUT_DEVICE_H

#include <stdint.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <taiwins/objects/seat_grab.h>
#include <taiwins/objects/gestures.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_input_device_type {
	TW_INPUT_TYPE_KEYBOARD,
	TW_INPUT_TYPE_POINTER,
	TW_INPUT_TYPE_TOUCH,
	TW_INPUT_TYPE_TABLET_TOOL,
	TW_INPUT_TYPE_TABLET_PAD,
	TW_INPUT_TYPE_SWITCH,
};

struct tw_input_device;

/**
 * @brief input event bus provides the many-sources-to-many-dests events
 * forwarding.
 */
struct tw_input_source {
	struct wl_signal remove; /* device remove */
	struct wl_signal event; /* an input event emitted */

	struct {
		struct wl_signal key;
		struct wl_signal modifiers;
		struct wl_signal keymap;
		//Maybe we should have this
		//struct wl_signal keymap_request;
	} keyboard;

	struct {
		struct wl_signal motion;
		struct wl_signal motion_absolute;
		struct wl_signal button;
		struct wl_signal axis;
		struct wl_signal frame;
		struct wl_signal swipe_begin;
		struct wl_signal swipe_update;
		struct wl_signal swipe_end;
		struct wl_signal pinch_begin;
		struct wl_signal pinch_update;
		struct wl_signal pinch_end;
	} pointer;

	struct {
		struct wl_signal down;
		struct wl_signal up;
		struct wl_signal motion;
		struct wl_signal cancel;
	} touch;
};

/**
 * @brief this struct mirrors the tw_input_source for the listener side.
 */
struct tw_input_sink {
	struct wl_listener remove; /* device remove */
	struct wl_listener event; /* an input event emitted */

	struct {
		struct wl_listener key;
		struct wl_listener modifiers;
		struct wl_listener keymap;
		//Maybe we should have this
		//struct wl_listener keymap_request;

	} keyboard;

	struct {
		struct wl_listener motion;
		struct wl_listener motion_absolute;
		struct wl_listener button;
		struct wl_listener axis;
		struct wl_listener frame;
		struct wl_listener swipe_begin;
		struct wl_listener swipe_update;
		struct wl_listener swipe_end;
		struct wl_listener pinch_begin;
		struct wl_listener pinch_update;
		struct wl_listener pinch_end;
	} pointer;

	struct {
		struct wl_listener down;
		struct wl_listener up;
		struct wl_listener motion;
		struct wl_listener cancel;
	} touch;
};

struct tw_keyboard_input {
	xkb_led_mask_t depressed, latched, locked, group;

	struct xkb_keymap *keymap;
	struct xkb_state *keystate;
};

struct tw_tablet_pad_input {
	size_t btn_count, ring_count, strip_count;
};

struct tw_input_device_impl {
	void (*destroy)(struct tw_input_device *dev);
};

/**
 * @brief a input device represents abstract input devices drives the input
 * events
 *
 * TODO: do I really need this destroy? Maybe I need to get rid of it.
 */
struct tw_input_device {
	enum tw_input_device_type type;
	uint32_t seat_id;
	unsigned int vendor, product;
	char name[32];

	/** available states for the specific device, some devices do not need
	 * states */
	union {
		struct tw_keyboard_input keyboard;
		struct tw_tablet_pad_input tablet;

	} input;

	struct tw_input_source *emitter;
	struct wl_list link; /* backend:inputs */
	const struct tw_input_device_impl *impl;
};

struct tw_output_device;

/**
 * Emit input signals
 *
 * This macro emit an input signal, it notifies the specific event and also
 * signal an input event is delivered. This makes it is easier to implement
 * mechanisms like input inhibit
 */
#define tw_input_signal_emit(src, sig, ev) \
	do { \
		wl_signal_emit(&src->sig, ev); \
		wl_signal_emit(&src->event, src); \
	} while (0)

/**
 * @brief attach the given input device to an emitter.
 *
 * multiple input devices can attach to the same emitter, the emitter can also
 * be subscribed by multiple tw_input_sink. This way tw_input_device can handle
 * many-sources-to-many-dests dependency.
 */
void
tw_input_device_attach_emitter(struct tw_input_device *device,
                               struct tw_input_source *emitter);
void
tw_input_device_init(struct tw_input_device *device,
                     enum tw_input_device_type type,
                     uint32_t seat_id,
                     const struct tw_input_device_impl *impl);
void
tw_input_device_fini(struct tw_input_device *device);

/* some backend would provide keymap themselves */
void
tw_input_device_set_keymap(struct tw_input_device *device,
                           struct xkb_keymap *keymap);
void
tw_input_source_init(struct tw_input_source *source);

void
tw_input_device_notify_key(struct tw_input_device *dev,
                           struct tw_event_keyboard_key *key);
void
tw_input_device_notify_modifiers(struct tw_input_device *dev,
                                 struct tw_event_keyboard_modifier *mod);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
