/*
 * data_device.c - taiwins server wl_data_device implementation
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
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <ctypes/helpers.h>

#include "data_device.h"
#include "objects/seat.h"

static struct tw_data_device_manager s_tw_data_device_manager;

/******************************************************************************
 * tw_data_source implemenation
 *****************************************************************************/

struct tw_data_source {
	struct wl_resource *resource;
	struct wl_array mimes;
	uint32_t actions;
};

static const struct wl_data_source_interface data_source_impl;

static struct tw_data_source *
tw_data_source_from_resource(struct wl_resource *resource)
{
	wl_resource_instance_of(resource, &wl_data_source_interface,
	                        &data_source_impl);
	return wl_resource_get_user_data(resource);
}

static struct tw_data_source *
tw_data_source_create(void)
{
	struct tw_data_source *source =
		calloc(1, sizeof(struct tw_data_source));
	if (!source)
		return NULL;
	wl_array_init(&source->mimes);
	source->actions = 0;
	return source;
}

static void
tw_data_source_destroy(struct wl_resource *resource)
{
	char *mime_type;
	struct tw_data_source *source = tw_data_source_from_resource(resource);
	wl_array_for_each(mime_type, &source->mimes)
		free(mime_type);
	wl_array_release(&source->mimes);
	source->actions = -1;
	free(source);
}

static void
data_source_offer(struct wl_client *client,
                  struct wl_resource *resource,
                  const char *mime_type)
{
	struct tw_data_source *data_source =
		tw_data_source_from_resource(resource);
	char **new_mime_type =
		wl_array_add(&data_source->mimes, sizeof(char *));
	if (new_mime_type)
		*new_mime_type = strdup(mime_type);
}

static void
data_source_set_actions(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t dnd_actions)
{
	struct tw_data_source *data_source =
		tw_data_source_from_resource(resource);
	data_source->actions = dnd_actions;
}

static void
data_source_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

/******************************************************************************
 * wl_data_offer implemenation
 *****************************************************************************/
struct tw_data_offer {
	struct tw_data_source *source;
};


/******************************************************************************
 * wl_data_device implemenation
 *****************************************************************************/

struct tw_data_device {
	struct tw_seat *seat;
	struct wl_resource *resource;
	struct tw_data_source *source_set;
	struct tw_data_offer *offer;
	/* data_device would create a new data offer for clients */
	struct wl_listener create_data_offer;
};

static struct tw_data_device *
tw_data_device_from_source(struct wl_resource *resource);

static void
create_data_offer(struct wl_listener *listener, void *data)
{
	struct wl_resource *data_offer_resource;
	struct tw_data_device *device =
		container_of(listener, struct tw_data_device,
		             create_data_offer);
	struct wl_client *client = wl_resource_get_client(device->resource);
	uint32_t version = wl_resource_get_version(device->resource);
	data_offer_resource = wl_resource_create(client, &wl_data_offer_interface,
	                                         version, 0);
}

static void
data_device_start_drag(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *source,
                       struct wl_resource *origin,
                       struct wl_resource *icon,
                       uint32_t serial)
{
}

static void
data_device_set_selection(struct wl_client *client,
                          struct wl_resource *device_resource,
                          struct wl_resource *source_resource,
                          uint32_t serial)
{
	struct tw_data_source *source =
		tw_data_source_from_resource(source_resource);
	struct tw_data_device *device =
		tw_data_device_from_source(device_resource);

	if (source->actions) {
		wl_resource_post_error(source_resource,
		                       WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
		                       "attempted to set selection on a "
		                       "dnd source");
		return;
	}
	//maybe we shall check if source is set?
	device->source_set = source;
	//remove the previous focus signal for eviting creating duplicate
	//wl_data_offer
	wl_list_remove(&device->create_data_offer.link);
	wl_list_init(&device->create_data_offer.link);
	wl_signal_add(&device->seat->focus_signal, &device->create_data_offer);
}

static void
data_device_release(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_data_device *device =
		tw_data_device_from_source(resource);
	device->source_set = NULL;
}

static const struct wl_data_device_interface data_device_impl = {
	.start_drag = data_device_start_drag,
	.set_selection = data_device_set_selection,
	.release = data_device_release,
};

static struct tw_data_device *
tw_data_device_from_source(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wl_data_device_interface,
	                               &data_device_impl));
	return wl_resource_get_user_data(resource);
}

static void
destroy_data_device_resource(struct wl_resource *resource)
{
	struct tw_data_device *device =
		tw_data_device_from_source(resource);
	free(device);
}

/******************************************************************************
 * wl_data_device_manager implemenation
 *****************************************************************************/

static void
create_data_source(struct wl_client *client,
                   struct wl_resource *manager_resource,
                   uint32_t id)
{
	struct tw_data_source *data_source;
	struct wl_resource *data_source_resource;
	uint32_t version = wl_resource_get_version(manager_resource);

	data_source = tw_data_source_create();
	data_source_resource =
		wl_resource_create(client, &wl_data_source_interface,
		                   version, id);
	if (!data_source_resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	data_source->resource = data_source_resource;
	wl_resource_set_implementation(data_source_resource,
	                               &data_source_impl, data_source,
	                               tw_data_source_destroy);
}

static void
get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource)
{
	struct wl_resource *device_resource;
	struct tw_data_device *device;
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);

	assert(seat);

	device = calloc(1, sizeof(struct tw_data_device));
	if (!device) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	device_resource = wl_resource_create(client, &wl_data_device_interface,
	                                     version, id);
	if (!device_resource) {
		free(device);
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(device_resource, &data_device_impl,
	                               device, destroy_data_device_resource);
	device->resource = device_resource;
	device->seat = seat;
	wl_list_init(&device->create_data_offer.link);
	device->create_data_offer.notify = create_data_offer;
}

static const struct wl_data_device_manager_interface data_device_manager_impl =
{
	.create_data_source = create_data_source,
	.get_data_device = get_data_device,
};

static void
bind_data_device_manager(struct wl_client *client,
                         void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &wl_data_device_manager_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &data_device_manager_impl,
	                               data, NULL);
}

static void
destroy_data_device_manager(struct wl_listener *listener, void *data)
{
	struct tw_data_device_manager *manager =
		container_of(listener, struct tw_data_device_manager,
		             display_destroy_listener);
	wl_global_destroy(manager->global);
}

struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display)
{
	struct tw_data_device_manager *manager =
		&s_tw_data_device_manager;

	manager->global = wl_global_create(display,
	                                   &wl_data_device_manager_interface,
	                                   3, manager,
	                                   bind_data_device_manager);
	if (!manager)
		return NULL;

	wl_list_init(&manager->clients);
	wl_list_init(&manager->display_destroy_listener.link);
	manager->display_destroy_listener.notify =
		destroy_data_device_manager;
	wl_display_add_destroy_listener(display,
	                                &manager->display_destroy_listener);
	return manager;
}
