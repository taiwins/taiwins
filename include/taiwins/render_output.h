/*
 * render_output.h - taiwins render output header
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

#ifndef TW_RENDER_OUTPUT_H
#define TW_RENDER_OUTPUT_H

#include <stdint.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <time.h>

#include "render_context.h"
#include "output_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_FRAME_TIME_CNT 8

struct tw_render_context;

enum tw_render_output_repaint_state {
	TW_REPAINT_CLEAN = 0, /**< repainted */
	TW_REPAINT_DIRTY = 1, /**< repaint required */
	TW_REPAINT_SCHEDULED = 2, /**< repaint scheduled */
	TW_REPAINT_COMMITTED = 6, /**< repaint done, need swap */
};

struct tw_render_output {
	struct tw_output_device device;
	struct tw_render_presentable surface;

	struct wl_list link; /**< ctx->output */
	struct wl_list views;
	struct wl_event_source *repaint_timer;
	/* important to set it for surface to be renderable */
	struct tw_render_context *ctx;

	/* render_output s'occupy with render_data of the output */
	struct {
		/**< we have 3 frame_damages for triple buffering */
		pixman_region32_t damages[3];
		pixman_region32_t *pending_damage, *curr_damage, *prev_damage;
		struct tw_mat3 view_2d; /* global to output space */

		uint32_t repaint_state;
	} state;

	struct {
		struct wl_listener set_mode; /* device::set_mode */
		struct wl_listener destroy; /* device::destroy */
	} listeners;

	/* TODO: maybe move this to engine_output? */
	struct {
		struct wl_signal surface_enter;
		struct wl_signal surface_leave;
		struct wl_signal need_frame;
		struct wl_signal pre_frame;
		struct wl_signal post_frame;
	} signals;
};

void
tw_render_output_init(struct tw_render_output *output,
                      const struct tw_output_device_impl *impl,
                      struct wl_display *display);
void
tw_render_output_fini(struct tw_render_output *output);

void
tw_render_output_set_context(struct tw_render_output *output,
                             struct tw_render_context *ctx);
void
tw_render_output_unset_context(struct tw_render_output *output);

void
tw_render_output_dirty(struct tw_render_output *output);

/**
 * @brief flush frame will send wl_callback::done for the wl_surfaces.
 *
 * It is enssential to call this at some point after the frame rendering is
 * done as wayland clients reply on this to draw next frame.
 */
void
tw_render_output_flush_frame(struct tw_render_output *output,
                             const struct timespec *now);
/**
 * @brief posting a render request to render_output
 *
 * User may call this upon receiving a need_frame signal to trigger the actual
 * rendering, the render_output would repaint if there is no repainting started
 * already
 */
void
tw_render_output_post_frame(struct tw_render_output *output);

//TODO: don't expose this, only used by backends
void
tw_render_output_clean_maybe(struct tw_render_output *output);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
