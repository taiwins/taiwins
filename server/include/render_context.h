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

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_options;
struct tw_render_pipeline;
struct tw_render_context;
struct tw_render_surface;
struct tw_render_texture;

enum tw_renderer_type {
	TW_RENDERER_EGL,
	TW_RENDERER_VK,
};

struct tw_render_context_impl {
	bool (*new_offscreen_surface)(struct tw_render_surface *surf,
	                              struct tw_render_context *ctx,
	                              unsigned int width, unsigned int height);

	bool (*new_window_surface)(struct tw_render_surface *surf,
	                           struct tw_render_context *ctx,
	                           void *native_window);

	bool (*commit_surface)(struct tw_render_surface *surf,
	                       struct tw_render_context *ctx);

        int (*make_current)(struct tw_render_surface *surf,
	                    struct tw_render_context *ctx);
};

/* we create this render context from scratch so we don't break everything, the
 * backends are still hooking to wlroots for now, they are created with a
 * tw_renderer, we will move backend on to this when we have our first backend.
 *
 */
struct tw_render_context {
	enum tw_renderer_type type;

	const struct tw_render_context_impl *impl;
	struct wl_display *display;
	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
		struct wl_signal dma_set;
		struct wl_signal compositor_set;
		//this is plain damn weird.
		struct wl_signal wl_surface_dirty;
		struct wl_signal wl_surface_destroy;
	} events;

	struct wl_list pipelines;
};

/* pipeline represents a collection of framebuffer shaders */
struct tw_render_pipeline {
	const char *name;
	struct tw_render_context *ctx;
	struct wl_list link;

};

/* a render surface for backend to work with */
//TODO: rename it tw_render_presentable, and it should have a present
struct tw_render_surface {
	intptr_t handle;
	void (*destroy)(struct tw_render_surface *surface,
	                struct tw_render_context *ctx);
};

struct tw_render_texture {
	uint32_t width, height;
	int fmt;
	bool has_alpha, inverted_y;
	enum wl_shm_format wl_format;

	void (*destroy)(struct tw_render_texture *tex,
	                struct tw_render_context *ctx);
};

/**
 * @brief render data for wl_surface, created
 */
struct tw_render_wl_surface {
	struct tw_render_context *ctx;
	struct tw_surface *surface;
	pixman_region32_t clip;

#ifdef TW_OVERLAY_PLANE
	pixman_region32_t output_damage[32];
#endif
	/** used if surface is on layers */
	struct wl_list layer_link;

	struct {
		struct wl_listener destroy;
		struct wl_listener commit;
		struct wl_listener frame;
		struct wl_listener dirty;
	} listeners;
};

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts);

//TODO implement this later when vulkan is enough to work with
struct tw_render_context *
tw_render_context_create_vk(struct wl_display *display);

void
tw_render_context_destroy(struct tw_render_context *ctx);

void
tw_render_context_set_dma(struct tw_render_context *ctx,
                          struct tw_linux_dmabuf *dma);
void
tw_render_context_set_compositor(struct tw_render_context *ctx,
                                 struct tw_compositor *compositor);
static inline bool
tw_render_surface_init_offscreen(struct tw_render_surface *surface,
                                 struct tw_render_context *ctx,
                                 unsigned int width, unsigned int height)
{
	return ctx->impl->new_offscreen_surface(surface, ctx, width, height);
}

static inline bool
tw_render_surface_init_window(struct tw_render_surface *surf,
                              struct tw_render_context *ctx,
                              void *native_window)
{
	return ctx->impl->new_window_surface(surf, ctx, native_window);
}

static inline void
tw_render_surface_fini(struct tw_render_surface *surface,
                       struct tw_render_context *ctx)
{
	surface->destroy(surface, ctx);
}

static inline bool
tw_render_surface_commit(struct tw_render_surface *surface,
                         struct tw_render_context *ctx)
{
	return ctx->impl->commit_surface(surface, ctx);
}

int
tw_render_surface_make_current(struct tw_render_surface *surf,
                               struct tw_render_context *ctx);

void
tw_render_init_wl_surface(struct tw_render_wl_surface *surface,
                          struct tw_surface *tw_surface,
                          struct tw_render_context *ctx);
void
tw_render_fini_wl_surface(struct tw_render_wl_surface *surface);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
