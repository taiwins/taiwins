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
	struct wl_listener parent_destroyed;
	int32_t sx, sy;
	bool sync;
	const struct tw_allocator *alloc;
};

bool
tw_surface_is_subsurface(struct tw_surface *surf, bool include_exotic);

bool
tw_subsurface_is_synched(struct tw_subsurface *sub);

struct tw_subsurface *
tw_surface_get_subsurface(struct tw_surface *surf);

/**
 * @brief init a subsurface like object; memory may not be managed
 *
 * It is required to call tw_subsurface_fini in the notifier, otherwise you may
 * have undefined behavior.
 *
 * It is now possible to initialize a subsurface without a parent, such
 * subsurface is HIDDEN. The "parenting" logic is handled through
 * tw_subsurface_show and tw_subsurface_hide.
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

/**
 * @brief remove a subsurface from its parent, subsurface has to be initialized
 */
void
tw_subsurface_hide(struct tw_subsurface *sub);

/**
 * @brief pair a subsurface to a parent, subsurface has to be initialized
 */
void
tw_subsurface_show(struct tw_subsurface *sub, struct tw_surface *parent);

/**
 * @brief declare a role as exotic subsurface role for other implementation of
 * subsurface.
 *
 * Another types implements subsurfaces can mark itself as an exotic
 * subsurface, then tw_surface_is_subsurface(surf, true) will consider it.
 *
 * To make it work. commit_private has to be a subsurface, otherwise the
 * behavior is undefined!
 */
void
tw_subsurface_add_role(struct tw_surface_role *role);

struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t version, uint32_t id,
                     struct tw_surface *surface, struct tw_surface *parent,
                     const struct tw_allocator *alloc);
void
tw_subsurface_update_pos(struct tw_subsurface *sub,
                         int32_t sx, int32_t sy);

#endif /* EOF */
