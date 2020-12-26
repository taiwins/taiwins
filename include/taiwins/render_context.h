/*
 * render_context.h - taiwins render context
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

#ifndef TW_RENDER_CONTEXT_H
#define TW_RENDER_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>

#include "render_presentable.h"
#include "render_wsi.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_options;
struct tw_render_context;
struct tw_render_surface;
struct tw_render_presentable;
struct tw_render_texture;

enum tw_renderer_type {
	TW_RENDERER_EGL,
	TW_RENDERER_VK,
};

struct tw_render_texture {
	uint32_t width, height;
	int fmt;
	bool has_alpha, inverted_y;
	enum wl_shm_format wl_format;

	void (*destroy)(struct tw_render_texture *tex,
	                struct tw_render_context *ctx);
};

struct tw_render_context_impl {
	bool (*new_offscreen_surface)(struct tw_render_presentable *surf,
	                              struct tw_render_context *ctx,
	                              unsigned int width, unsigned int height);

	bool (*new_window_surface)(struct tw_render_presentable *surf,
	                           struct tw_render_context *ctx,
	                           void *native_window);
	bool (*new_wsi_surface)(struct tw_render_presentable *surf,
	                        struct tw_render_context *ctx, int nimgs,
	                        enum tw_render_wsi_type type,
	                        struct tw_render_allocator *allocator);
};

struct tw_render_context {
	enum tw_renderer_type type;

	const struct tw_render_context_impl *impl;
	struct wl_display *display;
	struct wl_listener display_destroy;

	struct wl_list outputs;

	struct {
		struct wl_signal destroy;
		struct wl_signal dma_set;
		struct wl_signal compositor_set;
		/** emit at commit_surface */
		struct wl_signal presentable_commit;
		//this is plain damn weird.
		struct wl_signal wl_surface_dirty;
		struct wl_signal wl_surface_destroy;
	} events;

	struct wl_list pipelines;
};

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts);

//TODO implement this later
struct tw_render_context *
tw_render_context_create_vk(struct wl_display *display);

void
tw_render_context_destroy(struct tw_render_context *ctx);

void
tw_render_context_build_view_list(struct tw_render_context *ctx,
                                  struct tw_layers_manager *manager);
void
tw_render_context_set_dma(struct tw_render_context *ctx,
                          struct tw_linux_dmabuf *dma);
void
tw_render_context_set_compositor(struct tw_render_context *ctx,
                                 struct tw_compositor *compositor);

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
                                  void *native_window)
{
	return ctx->impl->new_window_surface(surf, ctx, native_window);
}

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
