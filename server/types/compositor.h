/*
 * compositor.h - taiwins server compositor header
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

#ifndef TW_COMPOSITOR_H
#define TW_COMPOSITOR_H

#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_compositor {
	struct wl_global *wl_compositor;
	struct wl_global *wl_subcompositor;
	struct wl_list clients;
	struct wl_list subcomp_clients;

	struct wl_signal surface_create;
	struct wl_signal region_create;
	struct wl_signal subsurface_get;

	struct wl_listener destroy_listener;
};

struct tw_event_new_wl_surface {
	struct wl_resource *compositor_res;
	struct wl_client *client;
	uint32_t version;
	uint32_t id;
};

struct tw_event_new_wl_region {
	struct wl_resource *compositor_res;
	struct wl_client *client;
	uint32_t version;
	uint32_t id;
};

struct tw_event_get_wl_subsurface {
	struct wl_resource *surface;
	struct wl_resource *parent_surface;
	uint32_t id;
};

struct tw_compositor *
tw_compositor_create_global(struct wl_display *display);


#ifdef  __cplusplus
}
#endif



#endif /* EOF */
