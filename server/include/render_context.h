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
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_options;
struct tw_render_pipeline;
struct tw_render_context;
struct tw_render_surface;

enum tw_renderer_type {
	TW_RENDERER_EGL,
	TW_RENDERER_VK,
};

struct tw_render_context_impl {
	bool (*new_offscreen_surface)(struct tw_render_context *ctx,
	                              intptr_t *surf, unsigned int width,
	                              unsigned int height);
	bool (*new_window_surface)(struct tw_render_context *ctx,
	                           intptr_t *surf, void *native_window);

	bool (*commit_surface)(struct tw_render_context *ctx,
	                       struct tw_render_surface *surf);
};

/* we create this render context from scratch so we don't break everything, the
 * backends are still hooking to wlroots for now, they are created with a
 * tw_renderer, we will move backend on to this when we have our first backend.
 *
 */
struct tw_render_context {
	enum tw_renderer_type type;

	struct tw_render_context_impl *impl;
	struct wl_display *display;
	struct wl_listener display_destroy;

	struct wl_list pipelines;
};

/* pipeline represents a collection of framebuffer shaders */
struct tw_render_pipeline {
	const char *name;
	struct tw_render_context *ctx;

	struct wl_list link;

};

/* a render surface for backend to work with */
struct tw_render_surface {
	intptr_t handle;
	void (*destroy)(struct tw_render_context *ctx,
	                struct tw_render_surface *surface);
};

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts);

//TODO implement this later when vulkan is enough to work with
struct tw_render_context *
tw_render_context_create_vk(struct wl_display *display);

void
tw_render_context_destroy(struct tw_render_context *ctx);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
