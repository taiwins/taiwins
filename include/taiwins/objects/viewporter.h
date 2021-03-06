/*
 * viewporter.h - taiwins wp_viewporter headers
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

#ifndef TW_VIEWPORTER_H
#define TW_VIEWPORTER_H

#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#include "surface.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_viewport {
	struct wl_resource *resource;
	struct tw_surface *surface;

	struct wl_listener surface_destroy_listener;
};

struct tw_viewporter {
	struct wl_global *globals;
	struct wl_listener display_destroy_listener;
};

struct tw_viewporter *
tw_viewporter_create_global(struct wl_display *display);

bool
tw_viewporter_init(struct tw_viewporter *viewporter,
                   struct wl_display *display);


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
