/*
 * render_surface.h - taiwins render context
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

#ifndef TW_RENDER_SURFACE_H
#define TW_RENDER_SURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>
#include <wayland-server.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/surface.h>

#include "render_context.h"

#define TW_VIEW_GLOBAL_LINK 1
#define TW_VIEW_OUTPUT_LINK 2

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @brief render data for wl_surface, created
 */
struct tw_render_surface {
	struct tw_surface surface;
	struct tw_render_context *ctx;
	pixman_region32_t clip;

	int32_t output; /**< the primary output for this surface */
	uint32_t output_mask; /**< the output it touches */

#ifdef TW_OVERLAY_PLANE
	pixman_region32_t output_damage[32];
#endif

	struct {
		struct wl_listener destroy;
		struct wl_listener commit;
		struct wl_listener frame;
		struct wl_listener dirty;
	} listeners;
};

void
tw_render_surface_init(struct tw_render_surface *surface,
                       struct tw_render_context *ctx);
void
tw_render_surface_fini(struct tw_render_surface *surface);

/* return with on no exception */
struct tw_render_surface *
tw_render_surface_from_resource(struct wl_resource *resource);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
