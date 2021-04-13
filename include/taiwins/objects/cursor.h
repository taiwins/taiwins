/*
 * cursor.h - taiwins wl_cursor headers
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

#ifndef TW_CURSOR_H
#define TW_CURSOR_H

#include <stdint.h>
#include <wayland-server.h>
#include <pixman.h>

#include "surface.h"
#include "layers.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @brief constrain the cursor a bound.
 *
 * This will help to cursor to stay in a bound, also helps to implement cursor
 * wrapping operations.
 *
 * For testing, cursor will try a delta, if fails in any constrain, it can
 * apply the location. Oherwise it shall clamp to the edge.
 */
struct tw_cursor_constrain {
	pixman_region32_t region; /* simple rect */
	struct wl_list link;
};

/**
 * @brief tw_cursor
 *
 * the cursor implements either a software cursor or hardware cursor (in a
 * cursor plane)
 */
struct tw_cursor {
	int32_t hotspot_x, hotspot_y;
	float x, y;

	struct tw_surface *curr_surface;
	struct tw_layer *cursor_layer;

	struct wl_list constrains; /* tw_cursor_constrain:link */
	struct tw_cursor_constrain curr_wrap;
	struct wl_listener surface_destroy;
};

void
tw_cursor_init(struct tw_cursor *cursor, struct tw_layer *cursor_layer);

void
tw_cursor_fini(struct tw_cursor *cursor);

void
tw_cursor_move(struct tw_cursor *cursor, float dx, float dy);

void
tw_cursor_set_pos(struct tw_cursor *cursor, float nx, float ny);

void
tw_cursor_set_wrap(struct tw_cursor *cursor, int32_t x, int32_t y,
                   uint32_t width, uint32_t height);
void
tw_cursor_unset_wrap(struct tw_cursor *cursor);

void
tw_cursor_move_with_wrap(struct tw_cursor *cursor, float dx, float dy,
                         int32_t x, int32_t y, uint32_t width,
                         uint32_t height);
void
tw_cursor_set_surface(struct tw_cursor *cursor,
                      struct wl_resource *surface_resource,
                      struct wl_resource *pointer_resource,
                      int32_t hotspot_x, int32_t hotspot_y);
void
tw_cursor_unset_surface(struct tw_cursor *cursor);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
