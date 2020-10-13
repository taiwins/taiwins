/*
 * render_pipeline.h - taiwins render pipeline
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

#ifndef TW_RENDER_PIPELINE_H
#define TW_RENDER_PIPELINE_H

#include <assert.h>
#include <wayland-server.h>

#include "render_context.h"
#include "render_output.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* pipeline represents a collection of framebuffer shaders */
struct tw_render_pipeline {
	const char *name;
	struct tw_render_context *ctx;
	struct wl_list link;
	struct wl_listener ctx_destroy;
	//maybe viewport?

	struct {
		struct wl_signal pre_output_repaint;
		struct wl_signal post_output_repaint;
	} events;

	struct {
		void (*repaint_output)(struct tw_render_pipeline *pipeline,
		                       struct tw_render_output *output,
		                       int buffer_age);
		void (*destroy)(struct tw_render_pipeline *pipeline);
	} impl;
};

void
tw_render_pipeline_init(struct tw_render_pipeline *pipeline,
                        const char *name, struct tw_render_context *ctx);
void
tw_render_pipeline_fini(struct tw_render_pipeline *pipeline);

struct tw_render_pipeline *
tw_egl_render_pipeline_create_default(struct tw_render_context *ctx,
                                      struct tw_layers_manager *manager);
static inline void
tw_render_pipeline_destroy(struct tw_render_pipeline *pipeline)
{
	assert(pipeline->impl.destroy);
	pipeline->impl.destroy(pipeline);
}

static inline void
tw_render_pipeline_repaint(struct tw_render_pipeline *pipeline,
                           struct tw_render_output *output,
                           int buffer_age)
{
	assert(pipeline->impl.repaint_output);
	pipeline->impl.repaint_output(pipeline, output, buffer_age);
}

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
