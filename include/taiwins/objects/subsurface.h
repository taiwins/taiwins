/*
 * subsurface.h - taiwins wl_subsurface headers
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

#ifndef TW_SUBSURFACE_H
#define TW_SUBSURFACE_H

#include "surface.h"


/** a good reference about subsurface is here
 * :https://ppaalanen.blogspot.com/2013/11/sub-surfaces-now.html
 */
struct tw_subsurface {
	struct wl_resource *resource;
	struct tw_surface *surface;
	struct tw_surface *parent;
	struct wl_list parent_link; /**< reflects subsurface stacking order */
	struct wl_list parent_pending_link; /* accummulated stacking order */
	struct wl_signal destroy;
	struct wl_listener surface_destroyed;
	int32_t sx, sy;
	bool sync;
	const struct tw_allocator *alloc;
};

bool
tw_surface_is_subsurface(struct tw_surface *surf);

bool
tw_subsurface_is_synched(struct tw_subsurface *sub);

struct tw_subsurface *
tw_surface_get_subsurface(struct tw_surface *surf);

/**
 * @brief  init a subsurface like object; memory may not be managed
 */
void
tw_subsurface_init(struct tw_subsurface *sub, struct wl_resource *resource,
                   struct tw_surface *surface, struct tw_surface *parent,
                   wl_notify_func_t notifier);
/**
 * @brief cleanup the subsurface without freeing the object
 */
void
tw_subsurface_fini(struct tw_subsurface *sub);

struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t version, uint32_t id,
                     struct tw_surface *surface, struct tw_surface *parent,
                     const struct tw_allocator *alloc);
void
tw_subsurface_update_pos(struct tw_subsurface *sub,
                         int32_t sx, int32_t sy);

#endif /* EOF */
