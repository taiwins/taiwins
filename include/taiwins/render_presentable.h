/*
 * render_presentable.h - taiwins render presentable
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

#ifndef TW_RENDER_PRESENTABLE_H
#define TW_RENDER_PRESENTABLE_H

#include <stdbool.h>
#include <wayland-util.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_render_context;
struct tw_render_presentable;


struct tw_render_presentable_impl {
	void (*destroy)(struct tw_render_presentable *surface,
	                struct tw_render_context *ctx);
	bool (*commit)(struct tw_render_presentable *surf,
	               struct tw_render_context *ctx);
        int (*make_current)(struct tw_render_presentable *surf,
	                    struct tw_render_context *ctx);
};

struct tw_render_presentable {
	intptr_t handle;
	const struct tw_render_presentable_impl *impl;
};

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
	return surface->impl->commit(surface, ctx);
}

static inline int
tw_render_presentable_make_current(struct tw_render_presentable *surf,
                                   struct tw_render_context *ctx)
{
	return surf->impl->make_current(surf, ctx);
}


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
