/*
 * render.h - taiwins render context internal header
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


#ifndef TW_RENDER_INTERNAL_H
#define TW_RENDER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/render_pipeline.h>

#ifdef  __cplusplus
extern "C" {
#endif

/******************************************************************************
 * render_context
 *****************************************************************************/
bool
tw_render_context_init(struct tw_render_context *ctx,
                       struct wl_display *display,
                       enum tw_renderer_type type,
                       const struct tw_render_context_impl *impl);

/******************************************************************************
 * render_presentable
 *****************************************************************************/

static inline bool
tw_render_presentable_init_offscreen(struct tw_render_presentable *surface,
                                     struct tw_render_context *ctx,
                                     unsigned int width, unsigned int height)
{
	return ctx->impl->new_offscreen_surface(surface, ctx, width, height);
}

static inline bool
tw_render_presentable_init_window(struct tw_render_presentable *surf,
                                  struct tw_render_context *ctx,
                                  void *native_window, uint32_t fmt)
{
	return ctx->impl->new_window_surface(surf, ctx, native_window, fmt);
}

static inline void
tw_render_presentable_fini(struct tw_render_presentable *surface,
                           struct tw_render_context *ctx)
{
	surface->impl->destroy(surface, ctx);
	surface->handle = (intptr_t)NULL;
}

static inline bool
tw_render_presentable_commit(struct tw_render_presentable *surface,
                             struct tw_render_context *ctx)
{
	bool ret = true;

	if ((ret = surface->impl->commit(surface, ctx)))
		wl_signal_emit(&surface->commit, surface);
	return ret;
}

static inline int
tw_render_presentable_make_current(struct tw_render_presentable *surf,
                                   struct tw_render_context *ctx)
{
	return surf->impl->make_current(surf, ctx);
}

/******************************************************************************
 * render_pipeline
 *****************************************************************************/

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

/******************************************************************************
 * render_output
 *****************************************************************************/

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
tw_render_output_present(struct tw_render_output *output,
                         struct tw_event_output_present *event);

void
tw_render_output_clean_maybe(struct tw_render_output *output);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
