/*
 * backend.h - taiwins server backend header
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

#ifndef TW_BACKEND_H
#define TW_BACKEND_H

#include <stdint.h>
#include <strings.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/render/wlr_renderer.h>
#include <xkbcommon/xkbcommon.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend;
struct tw_backend_output;
struct tw_backend_seat;

enum tw_backend_event_type {
	TW_BACKEND_ADD_OUTPUT,
	TW_BACKEND_RM_OUTPUT,
	TW_BACKEND_ADD_SEAT,
	TW_BACKEND_RM_SEAT,
	TW_BACKEND_CH_SEAT,
};

struct tw_backend_output_mode {
	int32_t w, h, refresh;
};

/* we would have list of seats in  */
enum tw_input_device_cap {
	TW_INPUT_CAP_KEYBOARD = 1 << 0,
	TW_INPUT_CAP_POINTER = 1 << 1,
	TW_INPUT_CAP_TOUCH = 1 << 2,
	TW_INPUT_CAP_TABLET_TOOL = 1 << 3,
	TW_INPUT_CAP_TABLET_PAD = 1 << 4,
	TW_INPUT_CAP_SWITCH = 1 << 5,
	TW_INPUT_CAP_ALL = 0x1f,
};

/**
 * @brief backend output
 *
 * backend has the logic of output, for now, we take advantage of wlr_output,
 * which implements wl_output.
 */
struct tw_backend_output {
	struct tw_backend *backend;
	struct wlr_output *wlr_output;
	int32_t id, cloning;
	struct wl_list link;

	//we can do this, or we uses current state
	struct {
		bool dirty;
		bool activate;
		bool preferred_mode;
		int32_t x, y, w, h, refresh;
		float scale;
		enum wl_output_transform transform;
		//TODO set gamma, the gamma value is the typical exp value you
		//used for monitors, 1.0 means linear gamma. wlr uses a
		//different gamma method, we deal with later
		float gamma_value;

	} state;

	struct wl_listener frame_listener;
	struct wl_listener destroy_listener;
};

struct tw_backend_seat {
	int idx;
	struct wl_list link;
	uint32_t capabilities;
	struct tw_backend *backend;

	struct tw_seat *tw_seat; /**< tw_seat implments wl_seat protocol */
	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener modifiers;
		struct wl_listener key;
		struct wl_listener keymap;
		/** xkb info */
		struct xkb_rule_names rules;

	} keyboard;

	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener button;
		struct wl_listener motion;
		struct wl_listener axis;
		struct wl_listener frame;
	} pointer;

	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener down;
		struct wl_listener up;
		struct wl_listener motion;
		struct wl_listener cancel;
	} touch;
};

struct tw_backend {
	struct wl_display *display;
	struct wlr_backend *auto_backend;
	struct wlr_renderer *main_renderer;
	bool started;
	/**< options */
	bool defer_output_creation;

	struct wl_list heads;
	struct wl_list pending_heads;
	struct wl_listener head_add_listener;
	struct wl_listener input_add_listener;
	struct wl_listener compositor_destroy_listener;

        struct wl_signal output_plug_signal;
	struct wl_signal output_unplug_signal;
	struct wl_signal seat_add_signal;
	struct wl_signal seat_ch_signal;
	struct wl_signal seat_rm_signal;

	/* outputs */
	uint32_t output_pool;
	struct tw_backend_output outputs[32];

        /* inputs */
        /**< cursor is global, this is a design choice, you can also implement
         * per-seat cursor. The benefit of a single cursor is that you would not
         * be confused when clients attach images to the cursor. The wlr_cursor
         * is not really useful at the moment, we are not attaching any images
         * to it. */
	struct wlr_cursor *global_cursor;
	struct xkb_context *xkb_context;
	struct wl_list inputs;
	uint8_t seat_pool;
	struct tw_backend_seat seats[8];
};


struct tw_backend *
tw_backend_create_global(struct wl_display *display);

void
tw_backend_flush(struct tw_backend *backend);

/* get the wlr_backend or libweston_backend */
void *
tw_backend_get_backend(struct tw_backend *backend);

void
tw_backend_defer_outputs(struct tw_backend *backend, bool defer);

void
tw_backend_add_listener(struct tw_backend *backend,
                        enum tw_backend_event_type event,
                        struct wl_listener *listener);

struct tw_backend_output *
tw_backend_find_output(struct tw_backend *backend, const char *name);

void
tw_backend_set_output_scale(struct tw_backend_output *output, float scale);

void
tw_backend_set_output_transformation(struct tw_backend_output *output,
                                     enum wl_output_transform transform);
int
tw_backend_get_output_modes(struct tw_backend_output *output,
                            struct tw_backend_output_mode *modes);
void
tw_backend_set_output_mode(struct tw_backend_output *output,
                           const struct tw_backend_output_mode *mode);
void
tw_backend_set_output_position(struct tw_backend_output *output,
                               uint32_t x, uint32_t y);
void
tw_backend_output_clone(struct tw_backend_output *dst,
                        const struct tw_backend_output *src);
void
tw_backend_output_enable(struct tw_backend_output *output,
                         bool enable);
//gamma or color temperature ?
void
tw_backend_output_set_gamma(struct tw_backend_output *output,
                            float gamma);
void
tw_backend_seat_set_xkb_rules(struct tw_backend_seat *seat,
                              struct xkb_rule_names *rules);
void
tw_backend_set_repeat_info(struct tw_backend *backend,
                           unsigned int rate, unsigned int delay);
//give you the wlr_seat.
void *
tw_backend_seat_get_backend(struct tw_backend_seat *seat);



#ifdef  __cplusplus
}
#endif


#endif /* EOF */
