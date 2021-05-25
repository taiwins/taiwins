/*
 * engine.h - taiwins server engine header
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

#ifndef TW_ENGINE_H
#define TW_ENGINE_H

#include <stdint.h>
#include <strings.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include <taiwins/objects/matrix.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/output.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/cursor.h>
#include <taiwins/objects/presentation_feedback.h>
#include <taiwins/objects/viewporter.h>
#include <taiwins/objects/gestures.h>
#include <xkbcommon/xkbcommon.h>

#include "input_device.h"
#include "output_device.h"

#define TW_VIEW_GLOBAL_LINK 1
#define TW_VIEW_OUTPUT_LINK 2

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_engine;
struct tw_backend;

/**
 * @brief engine output
 *
 * engine has the logic of output, for now, we take advantage of wlr_output,
 * which implements wl_output.
 */
struct tw_engine_output {
	struct tw_engine *engine;
	struct tw_output_device *device;
	struct tw_output *tw_output;

	int id, cloning;
	struct wl_list link; /* tw_engine:heads */

	struct tw_cursor_constrain constrain;

	struct {
		struct wl_listener info;
		struct wl_listener set_mode;
		struct wl_listener destroy;
		struct wl_listener present;
	} listeners;
};

struct tw_engine_seat {
	int idx;
	struct tw_engine *engine;
	struct tw_seat *tw_seat; /**< tw_seat implments wl_seat protocol */

	struct tw_input_source source;
	struct tw_input_sink sink;
	struct wl_list link; /* tw_engine.inputs */

	struct xkb_rule_names keyboard_rule_names;
	struct xkb_keymap *keymap;

	struct {
		struct wl_listener focus;
		struct wl_listener unfocus;
	} listeners;
};

/**
 * @brief taiwins engine
 *
 * The engine connects multiple taiwins components and run them together
 */
struct tw_engine {
	struct wl_display *display;
	struct tw_backend *backend;
        /** for now backend is based on wlr_backends. Future when migrate, we
         * would used api like wayland_impl_tw_backend(struct tw_backend *);
         */
	bool started;

	/* outputs */
	struct wl_list heads; /* tw_backend_output:links */
	uint32_t output_pool;
	struct tw_engine_output outputs[32];

        /* inputs */
	struct xkb_context *xkb_context;
	struct wl_list inputs; /* tw_backend_seat:links */
	uint8_t seat_pool;
	struct tw_engine_seat seats[8], *focused_seat;

        /** cursor is global, like most desktop experience, the one reason is
         * that people want to fit cursor in the cursor plane.
         */
	struct tw_cursor global_cursor;

	//only way we can avoid include this many managers is using global
	//objects

	struct tw_layers_manager layers_manager;
	struct tw_compositor compositor_manager;
	struct tw_data_device_manager data_device_manager;
	struct tw_linux_dmabuf dma_engine;
	struct tw_presentation presentation;
	struct tw_viewporter viewporter;
	struct tw_gestures_manager gestures_manager;

	/* listeners */
	struct {
		struct wl_listener display_destroy;
		struct wl_listener new_output;
		struct wl_listener new_input;
		struct wl_listener backend_started;
		struct wl_listener surface_dirty;
	} listeners;
        /* signals */
	struct {
		struct wl_signal output_created;
		struct wl_signal output_resized;
		struct wl_signal output_remove;
		struct wl_signal seat_created;
		struct wl_signal seat_focused;
		struct wl_signal seat_remove;
		struct wl_signal seat_input;
	} signals;
};

struct tw_engine *
tw_engine_create_global(struct wl_display *display,
                        struct tw_backend *backend);
struct tw_engine_seat *
tw_engine_get_focused_seat(struct tw_engine *engine);

struct tw_engine_seat *
tw_engine_seat_from_seat(struct tw_engine *engine, struct tw_seat *seat);

void
tw_engine_seat_set_xkb_rules(struct tw_engine_seat *seat,
                             struct xkb_rule_names *rules);
struct tw_engine_output *
tw_engine_get_focused_output(struct tw_engine *engine);

struct tw_engine_output *
tw_engine_output_from_resource(struct tw_engine *engine,
                               struct wl_resource *resource);
struct tw_engine_output *
tw_engine_output_from_device(struct tw_engine *engine,
                             const struct tw_output_device *device);
void
tw_engine_output_notify_surface_enter(struct tw_engine_output *output,
                                      struct tw_surface *surface);
void
tw_engine_output_notify_surface_leave(struct tw_engine_output *output,
                                      struct tw_surface *surface);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
