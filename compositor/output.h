/*
 * output.h - taiwins server output headers
 *
 * Copyright (c) 2021 Xichen Zhou
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

#ifndef TAIWINS_OUTPUT_H
#define TAIWINS_OUTPUT_H

#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/engine.h>
#include <taiwins/render_context.h>
#include <taiwins/objects/compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_FRAME_TIME_CNT 8

//we shall see how this works
struct tw_server_output {
	struct tw_engine_output *output;

	struct {
		/** average frame time in microseconds */
		uint32_t fts[TW_FRAME_TIME_CNT], ft_idx;
		struct timespec ts; /** used for recording start of frame */
		struct timespec last_present;
		struct wl_event_source *frame_timer;
	} state;

	struct {
		/**< device signals */
		struct wl_listener destroy;
                struct wl_listener present;
		struct wl_listener clock_reset;
		/**< render_output signals */
		struct wl_listener need_frame;
		struct wl_listener pre_frame;
		struct wl_listener post_frame;
	} listeners;
};

struct tw_server_output_manager {

	struct tw_engine *engine;
	struct tw_render_context *ctx;

	//reflect to the engine
	struct tw_server_output outputs[32];

	struct {
		struct wl_listener context_destroy;
		struct wl_listener surface_dirty;
		struct wl_listener surface_lost;
		struct wl_listener new_output;
	} listeners;

};

struct tw_server_output_manager *
tw_server_output_manager_create_global(struct tw_engine *engine,
                                       struct tw_render_context *ctx);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
