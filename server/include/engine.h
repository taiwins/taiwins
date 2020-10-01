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
	struct wl_list views; /** tw_surface->output_link */
	char name[24];

	struct tw_cursor_constrain constrain;

	struct {
		bool dirty;
		/**< we have 3 frame_damages for triple buffering */
		pixman_region32_t damages[3];
		pixman_region32_t *pending_damage, *curr_damage, *prev_damage;

		/** the repaint status, the output repaint is driven by timer,
		 * in the future we may be able to drive it by idle event */
		enum {
			TW_REPAINT_CLEAN = 0, /**< no need to repaint */
			TW_REPAINT_DIRTY, /**< repaint required */
			TW_REPAINT_NOT_FINISHED /**< still in repaint */
		} repaint_state;
	} state;

	struct {
		struct wl_listener info;
		struct wl_listener frame;
		struct wl_listener set_mode;
		struct wl_listener destroy;
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
	struct wl_list pending_heads;
	uint32_t output_pool;
	struct tw_engine_output outputs[32];

        /* inputs */
	struct xkb_context *xkb_context;
	struct wl_list inputs; /* tw_backend_seat:links */
	uint8_t seat_pool;
	struct tw_engine_seat seats[8];

        /** cursor is global, like most desktop experience, the one reason is
         * that people want to fit cursor in the cursor plane.
         */
	struct tw_cursor global_cursor;

	//only way we can avoid include this many managers is using global
	//objects

	struct tw_surface_manager surface_manager;
	struct tw_layers_manager layers_manager;
	struct tw_compositor compositor_manager;
	struct tw_data_device_manager data_device_manager;
	struct tw_linux_dmabuf dma_engine;
	struct tw_presentation presentation;
	struct tw_viewporter viewporter;

	/* listeners */
	struct wl_listener display_destroy;
	struct wl_listener new_output;
	struct wl_listener new_input;

        /* signals */
	struct {
		struct wl_signal output_created;
		struct wl_signal output_remove;
		struct wl_signal seat_created;
		struct wl_signal seat_remove;
	} events;
};

struct tw_engine *
tw_engine_create_global(struct wl_display *display,
                        struct tw_backend *backend);
struct tw_engine_seat *
tw_engine_get_focused_seat(struct tw_engine *engine);

void
tw_engine_seat_set_xkb_rules(struct tw_engine_seat *seat,
                             struct xkb_rule_names *rules);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
