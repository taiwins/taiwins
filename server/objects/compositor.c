/*
 * compositor.c - taiwins compositor implementation
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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include <ctypes/helpers.h>
#include "compositor.h"

#define COMPOSITOR_VERSION 4
#define SUBCOMPOSITOR_VERSION 1
#define SURFACE_VERSION 4
#define SUBSURFACE_VERSION 1

static struct tw_compositor s_tw_compositor = {0};

/******************************************************************************
 * wl_subcompositor implementation
 *****************************************************************************/
static const struct wl_subcompositor_interface subcompositor_impl;

static void
destroy_wl_subcompositor(struct wl_client *wl_client,
                         struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
get_wl_subsurface(struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id,
                  struct wl_resource *surface,
                  struct wl_resource *parent)
{
	struct tw_compositor *compositor;
	struct tw_event_get_wl_subsurface event = {
		.surface = surface,
		.parent_surface = parent,
		.client = client,
		.version = SUBSURFACE_VERSION,
		.id = id,
	};
	assert(wl_resource_instance_of(resource, &wl_subcompositor_interface,
	                               &subcompositor_impl));
	compositor = wl_resource_get_user_data(resource);

        //subcompositor is deeply linked to actual surface implementation. Here
	//we cannot directly do anything except making a signal proxy.
	//a subsurface resource is expected to be created in the event.
	wl_signal_emit(&compositor->subsurface_get, &event);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
	.destroy = destroy_wl_subcompositor,
	.get_subsurface = get_wl_subsurface,
};

static void
destroy_subcompositor(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
bind_subcompositor(struct wl_client *client, void *data,
                   uint32_t version, uint32_t id)
{
	struct tw_compositor *compositor = data;
	struct wl_resource *res =
		wl_resource_create(client, &wl_subcompositor_interface,
		                   version, id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &subcompositor_impl, compositor,
	                               destroy_subcompositor);

	wl_list_insert(compositor->subcomp_clients.prev,
	               wl_resource_get_link(res));
}

/******************************************************************************
 * wl_compositor implementation
 *****************************************************************************/

static const struct wl_compositor_interface compositor_impl;

static void
create_wl_surface(struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id)
{
	struct tw_compositor *compositor;
	struct tw_event_new_wl_surface event = {
		resource, client,
		SURFACE_VERSION,
		id,
	};

	assert(wl_resource_instance_of(resource, &wl_compositor_interface,
	                               &compositor_impl));
	compositor = wl_resource_get_user_data(resource);
	wl_signal_emit(&compositor->surface_create, &event);
}

static void
create_wl_region(struct wl_client *client,
                 struct wl_resource *resource,
                 uint32_t id)
{
	struct tw_compositor *compositor;
	struct tw_event_new_wl_region event = {
		resource, client,
		wl_resource_get_version(resource),
		id,
	};

	assert(wl_resource_instance_of(resource, &wl_compositor_interface,
	                               &compositor_impl));
	compositor = wl_resource_get_user_data(resource);
	wl_signal_emit(&compositor->region_create, &event);
}

static const struct wl_compositor_interface compositor_impl = {
	.create_surface = create_wl_surface,
	.create_region = create_wl_region,
};

static void
destroy_compositor_client(struct wl_resource *res)
{
	wl_list_remove(wl_resource_get_link(res));
}

static void
bind_compositor(struct wl_client *wl_client, void *data,
                uint32_t version, uint32_t id)
{
	struct wl_resource *res = NULL;
	struct tw_compositor *compositor = data;
	res = wl_resource_create(wl_client, &wl_compositor_interface,
	                         version, id);
	if (!res) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_resource_set_implementation(res, &compositor_impl, compositor,
	                               destroy_compositor_client);
	wl_list_insert(compositor->clients.prev,
		wl_resource_get_link(res));
}

static void
destroy_tw_compositor(struct wl_listener *listener, void *data)
{
	struct tw_compositor *compositor =
		container_of(listener, struct tw_compositor, destroy_listener);
	struct wl_resource *res, *next;

	wl_resource_for_each_safe(res, next, &compositor->clients)
		wl_resource_destroy(res);
	wl_resource_for_each_safe(res, next, &compositor->subcomp_clients)
		wl_resource_destroy(res);

	wl_global_destroy(compositor->wl_compositor);
	wl_global_destroy(compositor->wl_subcompositor);
}

struct tw_compositor *
tw_compositor_create_global(struct wl_display *display)
{
	struct tw_compositor *compositor = &s_tw_compositor;
	if (compositor->wl_compositor || compositor->wl_subcompositor)
		return compositor;

	compositor->wl_compositor =
		wl_global_create(display, &wl_compositor_interface,
		                 COMPOSITOR_VERSION, compositor, bind_compositor);
	compositor->wl_subcompositor =
		wl_global_create(display, &wl_subcompositor_interface,
		                 SUBCOMPOSITOR_VERSION, compositor,
		                 bind_subcompositor);

	wl_list_init(&compositor->destroy_listener.link);
	compositor->destroy_listener.notify = destroy_tw_compositor;

	wl_signal_init(&compositor->surface_create);
	wl_signal_init(&compositor->region_create);
	wl_signal_init(&compositor->subsurface_get);
	wl_list_init(&compositor->clients);
	wl_list_init(&compositor->subcomp_clients);

	return compositor;
}
