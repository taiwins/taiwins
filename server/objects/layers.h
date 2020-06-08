/*
 * layers.h - taiwins server layers manager
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

#ifndef TW_LAYERS_H
#define TW_LAYERS_H

#include <wayland-server.h>

#include <taiwins.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_layers_manager {
	struct wl_display *display;
	struct wl_list layers;
	struct wl_list views;

	struct wl_listener destroy_listener;
	//global layers
	struct tw_layer cursor_layer;
};

struct tw_layers_manager *
tw_layers_manager_create_global(struct wl_display *display);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
