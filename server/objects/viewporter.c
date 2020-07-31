/*
 * viewporter.c - taiwins wp_viewporter implementation
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

#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wayland-viewporter-server-protocol.h>
#include <ctypes/helpers.h>

#include "viewporter.h"
#include "surface.h"

#define VIEWPORTER_VERSION 1

static struct tw_viewporter s_tw_viewporter = {0};

/******************************************************************************
 * viewport implementation
 *****************************************************************************/
static struct tw_viewport *
tw_viewport_from_resource(struct wl_resource *resource);

static void
viewport_handle_destroy(struct wl_client *client,
                        struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
viewport_set_source(struct wl_client *client,
                    struct wl_resource *resource,
                    wl_fixed_t x,
                    wl_fixed_t y,
                    wl_fixed_t width,
                    wl_fixed_t height)
{
	struct tw_viewport *viewport = tw_viewport_from_resource(resource);
	struct tw_surface *surface = viewport->surface;
	double sx, sy, sw, sh;
	sx = wl_fixed_to_double(x);
	sy = wl_fixed_to_double(y);
	sw = wl_fixed_to_double(width);
	sh = wl_fixed_to_double(height);

	if (!surface) {
		wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE,
		                       "viewport does not have a surface");
		return;
	} else if (sx == -1.0 && sy && sw == -1.0 && sh) {
		surface->pending->crop.x = 0;
		surface->pending->crop.y = 0;
		surface->pending->crop.w = 0;
		surface->pending->crop.h = 0;
		return;
	} else if (sx < 0.0 || sy < 0.0 || sw <= 0.0 || sh <= 0.0) {
		wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE,
		                       "invalid viewport.set_source value"
		                       " (%f,%f,%f,%f)",
		                       sw, sy, sw, sh);
		return;
	} else if ((sx + sw) > surface->buffer.width ||
	           (sy + sh) > surface->buffer.height) {
		wl_resource_post_error(resource, WP_VIEWPORT_ERROR_OUT_OF_BUFFER,
		                       "invalid viewport.set_source value"
		                       " (%f,%f,%f,%f)",
		                       sw, sy, sw, sh);
		return;
	}
	surface->pending->crop.x = (int)sx;
	surface->pending->crop.y = (int)sy;
	surface->pending->crop.w = (int)sw;
	surface->pending->crop.h = (int)sh;
}

static void
viewport_set_destination(struct wl_client *client,
                         struct wl_resource *resource,
                         int32_t width,
                         int32_t height)
{
	struct tw_viewport *viewport = tw_viewport_from_resource(resource);
	struct tw_surface *surface = viewport->surface;

	if (!surface) {
		wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE,
		                       "viewport does not have a surface");
		return;
	} else if (width == -1 && height == -1) {
		surface->pending->surface_scale.w = 0;
		surface->pending->surface_scale.h = 0;
		return;
	} else if (width <= 0 || height <= 0) {
		wl_resource_post_error(resource,
		                       WP_VIEWPORT_ERROR_BAD_VALUE,
		                       "invalid viewport.set_destination value"
		                       " (%d,%d)", width, height);
		return;
	}
	surface->pending->surface_scale.w = width;
	surface->pending->surface_scale.h = height;
}

static const struct wp_viewport_interface viewport_impl = {
	.destroy = viewport_handle_destroy,
	.set_source = viewport_set_source,
	.set_destination = viewport_set_destination,
};

static struct tw_viewport *
tw_viewport_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wp_viewport_interface,
	                               &viewport_impl));
	return wl_resource_get_user_data(resource);
}

static void
destroy_viewport_resource(struct wl_resource *resource)
{
	struct tw_viewport *viewport = tw_viewport_from_resource(resource);

	if (viewport->surface) {
		viewport->surface->pending->crop.x = 0;
		viewport->surface->pending->crop.y = 0;
		viewport->surface->pending->crop.w = 0;
		viewport->surface->pending->crop.h = 0;
		viewport->surface->pending->surface_scale.w = 0;
		viewport->surface->pending->surface_scale.h = 0;
	}
	wl_list_remove(&viewport->surface_destroy_listener.link);
	free(viewport);
}

static void
notify_viewport_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_viewport *viewport =
		container_of(listener, struct tw_viewport,
		             surface_destroy_listener);
	viewport->surface = NULL;
	//this is unecessary
	wl_list_remove(&viewport->surface_destroy_listener.link);
	wl_list_init(&viewport->surface_destroy_listener.link);
}

static void
tw_viewport_init(struct tw_viewport *viewport, struct wl_resource *resource,
                 struct tw_surface *surface)
{
	viewport->resource = resource;
	viewport->surface = surface;

	wl_list_init(&viewport->surface_destroy_listener.link);
	viewport->surface_destroy_listener.notify =
		notify_viewport_surface_destroy;
	wl_signal_add(&surface->events.destroy,
	              &viewport->surface_destroy_listener);
}

/******************************************************************************
 * viewporter implementation
 *****************************************************************************/

static void
viewporter_get_viewport(struct wl_client *client,
                        struct wl_resource *viewporter_res,
                        uint32_t id,
                        struct wl_resource *surface_res)
{
	struct wl_resource *viewport_res;
	struct tw_viewport *viewport;
	struct tw_surface *surface = tw_surface_from_resource(surface_res);
	uint32_t surface_id = wl_resource_get_id(surface_res);
	uint32_t version  = wl_resource_get_version(viewporter_res);

	if (wl_signal_get(&surface->events.destroy,
	                  notify_viewport_surface_destroy)) {
		wl_resource_post_error(viewporter_res,
		                       WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
		                       "surface %u already has a viewporter.",
		                       surface_id);
		return;
	}
	viewport = calloc(1, sizeof(struct tw_viewport));
	if (!viewport) {
		wl_resource_post_no_memory(viewporter_res);
		return;
	}
	viewport_res = wl_resource_create(client, &wp_viewport_interface,
	                                  version, id);
	if (!viewport_res) {
		wl_resource_post_no_memory(viewporter_res);
		free(viewport);
		return;
	}
	wl_resource_set_implementation(viewport_res, &viewport_impl, viewport,
	                               destroy_viewport_resource);
	tw_viewport_init(viewport, viewport_res, surface);
}

static void
viewporter_handle_destroy_viewporter(struct wl_client *client,
                                     struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static struct wp_viewporter_interface viewporter_impl = {
	.destroy = viewporter_handle_destroy_viewporter,
	.get_viewport = viewporter_get_viewport,
};

static void
destroy_viewporter_resource(struct wl_resource *resource)
{
}

static void
bind_viewporter(struct wl_client *client, void *data,
                uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client, &wp_viewporter_interface, version,
		                   id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &viewporter_impl, NULL,
	                               destroy_viewporter_resource);
}


static void
notify_display_destroy(struct wl_listener *listener, void *display)
{
	struct tw_viewporter *viewporter =
		container_of(listener, struct tw_viewporter,
		             display_destroy_listener);
	wl_global_destroy(viewporter->globals);
}

bool
tw_viewporter_init(struct tw_viewporter *viewporter,
                   struct wl_display *display)
{
	viewporter->globals =
		wl_global_create(display,
		                 &wp_viewporter_interface, VIEWPORTER_VERSION,
		                 NULL, bind_viewporter);
	if (!viewporter->globals)
		return false;
	wl_list_init(&viewporter->display_destroy_listener.link);
	viewporter->display_destroy_listener.notify = notify_display_destroy;
	wl_display_add_destroy_listener(display,
	                                &viewporter->display_destroy_listener);
	return true;
}

struct tw_viewporter *
tw_viewporter_create_global(struct wl_display *display)
{
	struct tw_viewporter *viewporter = &s_tw_viewporter;
	if (!tw_viewporter_init(viewporter, display))
		return NULL;

	return viewporter;
}
