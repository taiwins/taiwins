/*
 * taiwins.h - taiwins server shared header
 *
 * Copyright (c) 2019 Xichen Zhou
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

#ifndef TAIWINS_H
#define TAIWINS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <helpers.h>
#include <wayland-server.h>
#include <libweston/zalloc.h>
#include <libweston/libweston.h>

//the declarations we need to move back
void
weston_output_move(struct weston_output *output, int x, int y);


#if defined (INCLUDE_DESKTOP)
#include <libweston-desktop/libweston-desktop.h>
#endif

#if defined (INCLUDE_BACKEND)
#include <libweston/backend-drm.h>
#include <libweston/backend-wayland.h>
#include <libweston/backend-x11.h>
#include <libweston/windowed-output-api.h>
#endif

#include "../shared_config.h"


#ifdef  __cplusplus
extern "C" {
#endif


/**
 * /brief taiwins output information
 *
 * here we define some template structures. It is passed as pure data, and they
 * are not persistent. So don't store them as pointers.
 */
struct taiwins_output {
	struct weston_output *output;
	//available space used in desktop area. We don't have the configureation
	//code yet, once it is available, it can be used to create this struct.
	struct weston_geometry desktop_area;
	uint32_t inner_gap;
	uint32_t outer_gap;
};


///////////////////////// UTILS Functions ///////////////////////// this maybe a
//stupid idea to use weston prefix, since libweston could add the function with
//same name
static inline struct weston_output *
tw_get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;
	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}

static inline struct weston_seat *
tw_get_default_seat(struct weston_compositor *ec)
{
	if (wl_list_empty(&ec->seat_list))
		return NULL;
	//be careful with container of, it doesn't care the
	return container_of(ec->seat_list.next,
			    struct weston_seat, link);
}

static inline struct weston_surface *
tw_surface_from_resource(struct wl_resource *wl_surface)
{
	return (struct weston_surface *)wl_resource_get_user_data(wl_surface);
}

static inline struct weston_view *
tw_default_view_from_surface(struct weston_surface *surface)
{
	//return NULL if no view is
	return (surface->views.next != &surface->views) ?

		container_of(surface->views.next, struct weston_view, surface_link) :
		NULL;
}

static inline struct weston_view *
tw_view_from_surface_resource(struct wl_resource *wl_surface)
{
	return tw_default_view_from_surface(
		tw_surface_from_resource(wl_surface));
}

static inline void
tw_map_view(struct weston_view *view)
{
	view->surface->is_mapped = true;
	view->is_mapped = true;
}

struct wl_client *tw_launch_client(struct weston_compositor *ec, const char *path);
/* kill a client */
void tw_end_client(struct wl_client *client);


void tw_lose_surface_focus(struct weston_surface *surface);
void tw_focus_surface(struct weston_surface *surface);
struct weston_output *tw_get_focused_output(struct weston_compositor *compositor);


///////////////////////// UTILS Functions /////////////////////////



/* here are the experimental code, it may not be anyway useful */
bool tw_set_wl_surface(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *surface,
		       struct wl_resource *output,
		       struct wl_listener *surface_destroy_listener);

//two option to manipulate the view
void setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y);
void setup_ui_view(struct weston_view *view, struct weston_layer *layer, int x, int y);


//////////////////////// load weston modules //////////////////////////
void *tw_load_weston_module(const char *name, const char *entrypoint);


#ifdef  __cplusplus
}
#endif



#endif /* EOF */
