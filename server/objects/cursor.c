/*
 * cursor.c - taiwins wl_cursor implementaton
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

#include <stdint.h>
#include <string.h>
#include <wayland-server.h>
#include <pixman.h>

#include <ctypes/helpers.h>

#include <taiwins/objects/cursor.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>

#define TW_CURSOR_ROLE "tw_cursor_role"

static inline void
cursor_set_surface_pos(struct tw_cursor *cursor)
{
	struct tw_surface *surface = cursor->curr_surface;
	int32_t left = cursor->x - cursor->hotspot_x;
	int32_t top = cursor->y - cursor->hotspot_y;

        if (!cursor->curr_surface)
		return;
	tw_surface_set_position(surface, left, top);
}

static void
notify_cursor_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_cursor *cursor =
		container_of(listener, struct tw_cursor, surface_destroy);
	cursor->curr_surface = NULL;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
}

static void
commit_cursor_surface(struct tw_surface *surface)
{
	struct tw_cursor *cursor = surface->role.commit_private;
	if (cursor->curr_surface != surface)
		return;
	cursor_set_surface_pos(cursor);
}

void
tw_cursor_init(struct tw_cursor *cursor)
{
	memset(cursor, 0, sizeof(*cursor));
	wl_list_init(&cursor->constrains);

	//init the current wrap to infinit
	pixman_region32_init_rect(&cursor->curr_wrap.region,
	                          INT32_MIN, INT32_MIN,
	                          UINT32_MAX, UINT32_MAX);
	wl_list_init(&cursor->curr_wrap.link);
	wl_list_init(&cursor->surface_destroy.link);
	cursor->surface_destroy.notify = notify_cursor_surface_destroy;
}

void
tw_cursor_fini(struct tw_cursor *cursor)
{
	struct tw_cursor_constrain *con, *tmp;

	pixman_region32_fini(&cursor->curr_wrap.region);
	wl_list_for_each_safe(con, tmp, &cursor->constrains, link)
		wl_list_remove(&con->link);

	memset(cursor, 0, sizeof(*cursor));
}

void
tw_cursor_set_surface(struct tw_cursor *cursor,
                      struct wl_resource *surface_resource,
                      struct wl_resource *pointer_resource,
                      struct tw_layer *cursor_layer,
                      int32_t hotspot_x, int32_t hotspot_y)
{
	struct tw_surface *surface =
		tw_surface_from_resource(surface_resource);
	uint32_t surface_id = wl_resource_get_id(surface_resource);
	if (surface->role.commit &&
	    surface->role.commit != commit_cursor_surface) {
		wl_resource_post_error(pointer_resource, WL_POINTER_ERROR_ROLE,
		                       "wl_surface@%d already have a role",
		                       surface_id);
		return;
	}
	tw_cursor_unset_surface(cursor);

	surface->role.commit = commit_cursor_surface;
	surface->role.commit_private = cursor;
	surface->role.name = TW_CURSOR_ROLE;
	wl_resource_add_destroy_listener(surface_resource,
	                                 &cursor->surface_destroy);
	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;
	cursor->curr_surface = surface;
	if (cursor_layer)
		wl_list_insert(cursor_layer->views.prev,
		               &surface->links[TW_VIEW_LAYER_LINK]);
}

void
tw_cursor_unset_surface(struct tw_cursor *cursor)
{
	struct tw_surface *curr_surface = cursor->curr_surface;
	//remove current cursor surface
	if (curr_surface) {
		wl_list_remove(&curr_surface->links[TW_VIEW_LAYER_LINK]);
		wl_list_init(&curr_surface->links[TW_VIEW_LAYER_LINK]);

		wl_list_remove(&cursor->surface_destroy.link);
		wl_list_init(&cursor->surface_destroy.link);
	}
}

void
tw_cursor_set_wrap(struct tw_cursor *cursor, int32_t x, int32_t y,
                   uint32_t width, uint32_t height)
{
	pixman_region32_clear(&cursor->curr_wrap.region);
	pixman_region32_init_rect(&cursor->curr_wrap.region,
	                          x, y, width, height);
}

void
tw_cursor_unset_wrap(struct tw_cursor *cursor)
{
	pixman_region32_clear(&cursor->curr_wrap.region);
	pixman_region32_init_rect(&cursor->curr_wrap.region,
	                          INT32_MIN, INT32_MIN,
	                          UINT32_MAX, UINT32_MAX);
}

void
tw_cursor_move(struct tw_cursor *cursor, float dx, float dy)
{
	struct tw_cursor_constrain *constrain;
	float nx = cursor->x + dx;
	float ny = cursor->y + dy;
	bool inbound = wl_list_length(&cursor->constrains) ? false : true;

	wl_list_for_each(constrain, &cursor->constrains, link) {
		if (pixman_region32_contains_point(&constrain->region,
		                                   nx, ny, NULL)) {
			inbound = true;
			break;
		}
	}
	if (inbound &&
	    pixman_region32_contains_point(&cursor->curr_wrap.region,
	                                   nx, ny, NULL)) {
		goto set_pos;
	} else {
		tw_logl("cursor location (%d, %d) is out of bound", nx, ny);
		return;
	}

set_pos:
	cursor->x += dx;
	cursor->y += dy;
	cursor_set_surface_pos(cursor);
}

void
tw_cursor_move_with_wrap(struct tw_cursor *cursor, float dx, float dy,
                         int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	pixman_box32_t *e = pixman_region32_extents(&cursor->curr_wrap.region);

	tw_cursor_set_wrap(cursor, x, y, width, height);
	tw_cursor_move(cursor, dx, dy);
	tw_cursor_set_wrap(cursor, e->x1, e->y1, e->x2 - e->x1, e->y2 - e->y1);
}

void
tw_cursor_set_pos(struct tw_cursor *cursor, float nx, float ny)
{
	tw_cursor_move(cursor, nx - cursor->x, ny - cursor->y);
}
