/*
 * render_swi.h - taiwins render swapchain context
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

#ifndef TW_RENDER_WSI_H
#define TW_RENDER_WSI_H

#include <stdint.h>
#include <wayland-server.h>
#include <taiwins/objects/drm_formats.h>
#include <taiwins/objects/dmabuf.h>

#include "render_presentable.h"

#ifdef  __cplusplus
extern "C" {
#endif


#define TW_MAX_SWAP_IMGS 3


struct tw_render_allocator;
struct tw_render_wsi;
struct tw_render_swap_img;

enum tw_render_wsi_type {
	TW_RENDER_WSI_FIFO,
	TW_RENDER_WSI_MAILBOX,
};

struct tw_render_allocator_impl {
	bool (*allocate)(struct tw_render_allocator *allocator,
	                 struct tw_render_swap_img *img);
	void (*free)(struct tw_render_allocator *allocator,
	             struct tw_render_swap_img *img);
};

/**
 * @brief allocator seperates the buffer producer (like gbm ) from swapchain.
 */
struct tw_render_allocator {
	const struct tw_render_allocator_impl *impl;
	struct wl_signal destroy;
};

struct tw_render_swap_img {
	unsigned w, h;
	/** platform specific with plus auxillary handles */
	uintptr_t handle, handle1, handle2, handle3;
	struct wl_list link; /* tw_render_wsi::back */

	struct tw_dmabuf_attributes attrs;
};

/**
 * @brief a public implementation of `tw_render_presentable` with
 * WSI(swapchain)
 *
 * This is intended replace native type like EGLSurface/VkSurfaceKHR for
 * `tw_render_presentable`. For the reason that gbm_surface_lock_front_buffers
 * is not available on Vulkan.
 *
 * With this type, render context could implement specific `make_current` and
 * `commit_presentable`
 */
struct tw_render_wsi {
	unsigned cnt;
	enum tw_render_wsi_type type;
	struct tw_render_presentable *surface;

	struct wl_list back; /**< submitted back buffer list */
	struct wl_list free; /**< free buffer list */
	/** front buffer on the screen, current buffer used for drawing */
	struct tw_render_swap_img *front, *curr;
	struct tw_render_swap_img imgs[TW_MAX_SWAP_IMGS];
	/* allocator provides buffer allocation and purging */
	struct tw_render_allocator *allocator;
	struct wl_listener allocator_destroy;
};

bool
tw_render_wsi_init(struct tw_render_wsi *wsi, enum tw_render_wsi_type type,
                   unsigned n, struct tw_render_allocator *allocator);
bool
tw_render_wsi_aquire_img(struct tw_render_wsi *wsi);

void
tw_render_wsi_push(struct tw_render_wsi *wsi);

void
tw_render_wsi_swap_front(struct tw_render_wsi *wsi);

void
tw_render_allocator_init(struct tw_render_allocator *allocator,
                         const struct tw_render_allocator_impl *impl);
void
tw_render_allocator_fini(struct tw_render_allocator *allocator);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
