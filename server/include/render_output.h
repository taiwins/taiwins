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

#include "render_context.h"
#include "output_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_render_output {
	struct tw_output_device device;
	struct tw_render_presentable surface;

	struct wl_list link; /**< ctx->otuputs */
	struct wl_list views;
	/* important to set it for surface to be renderable */
	struct tw_render_context *ctx;

	/* render_output s'occupy with render_data of the output */
	struct {
		bool dirty;
		/**< we have 3 frame_damages for triple buffering */
		pixman_region32_t damages[3];
		pixman_region32_t *pending_damage, *curr_damage, *prev_damage;
		struct tw_mat3 view_2d; /* global to output space */

		/** the repaint status, the output repaint is driven by timer,
		 * in the future we may be able to drive it by idle event */
		enum {
			TW_REPAINT_CLEAN = 0, /**< no need to repaint */
			TW_REPAINT_DIRTY, /**< repaint required */
			TW_REPAINT_NOT_FINISHED /**< still in repaint */
		} repaint_state;
	} state;

	struct {
		struct wl_listener frame;
		struct wl_listener set_mode;
		struct wl_listener destroy;
		struct wl_listener surface_dirty;
	} listeners;
};

void
tw_render_output_init(struct tw_render_output *output,
                      const struct tw_output_device_impl *impl);
void
tw_render_output_fini(struct tw_render_output *output);

void
tw_render_output_set_context(struct tw_render_output *output,
                             struct tw_render_context *ctx);
void
tw_render_output_rebuild_view_mat(struct tw_render_output *output);

void
tw_render_output_dirty(struct tw_render_output *output);


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
