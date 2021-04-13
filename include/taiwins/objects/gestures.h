/*
 * gestures.h - taiwins wp_pointer_gestures headers
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

#ifndef TW_GESTURES_H
#define TW_GESTURES_H

#include <stdint.h>
#include <wayland-server.h>

#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_gestures_manager {
	struct wl_display *display;
	struct wl_global *global;

	struct wl_list pinchs;
	struct wl_list swipes;

	struct wl_listener display_destroy;
};

bool
tw_gestures_manager_init(struct tw_gestures_manager *manager,
                        struct wl_display *display);
struct tw_gestures_manager *
tw_gestures_manager_create_global(struct wl_display *display);

void
tw_gestures_swipe_begin(struct tw_gestures_manager *manager,
                        struct tw_pointer *pointer, uint32_t time,
                        struct wl_resource *surface, uint32_t fingers);
void
tw_gestures_swipe_update(struct tw_gestures_manager *manager,
                         struct tw_pointer *pointer, uint32_t time,
                         double dx, double dy);
void
tw_gestures_swipe_end(struct tw_gestures_manager *manager,
                      struct tw_pointer *pointer, uint32_t time, bool cancel);
void
tw_gestures_pinch_begin(struct tw_gestures_manager *manager,
                        struct tw_pointer *pointer, uint32_t time,
                        struct wl_resource *surface, uint32_t fingers);
void
tw_gestures_pinch_update(struct tw_gestures_manager *manager,
                         struct tw_pointer *pointer, uint32_t time,
                         double dx, double dy, double scale, double rotation);
void
tw_gestures_pinch_end(struct tw_gestures_manager *manager,
                      struct tw_pointer *pointer, uint32_t time, bool cancel);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
