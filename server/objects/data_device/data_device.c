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

#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>

static struct tw_data_device_manager s_tw_data_device_manager;

/******************************************************************************
 * tw_data_source implemenation
 *****************************************************************************/


/******************************************************************************
 * wl_data_offer implemenation
 *****************************************************************************/


/******************************************************************************
 * wl_data_device implemenation
 *****************************************************************************/


static struct tw_data_device *
tw_data_device_from_source(struct wl_resource *resource);

struct tw_data_offer *
tw_data_device_create_data_offer(struct tw_data_device *device,
                                 struct wl_resource *surface)
{
	struct wl_resource *data_offer_resource;
	struct tw_data_offer *data_offer;

	struct wl_client *client = wl_resource_get_client(device->resource);
	uint32_t version = wl_resource_get_version(device->resource);

	if (!device->source_set)
		return NULL;
	data_offer_resource = wl_resource_create(client, &wl_data_offer_interface,
	                                         version, 0);
	if (!data_offer_resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	if (!(data_offer = tw_data_offer_create(data_offer_resource,
	                                        device->source_set))) {
		wl_client_post_no_memory(client);
		wl_resource_destroy(data_offer_resource);
		return NULL;
	}
	//the data offer is created for this surface. Should be destroyed when
	//leaving
	data_offer->current_surface = surface;

	//send data offer
	const char *p;
	wl_data_device_send_data_offer(device->resource, data_offer_resource);
	wl_array_for_each(p, &device->source_set->mimes)
		wl_data_offer_send_offer(data_offer_resource, p);
	if (device->source_set->actions &&
	    version >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
		wl_data_offer_send_source_actions(data_offer_resource,
		                                  device->source_set->actions);
	return data_offer;
}

static void
data_device_selection_data_offer(struct wl_listener *listener,
                                 void *data)
{
	struct wl_resource *surface = data;
	struct tw_data_device *device =
		container_of(listener, struct tw_data_device,
		             create_data_offer);
	if (!device->source_set || !device->source_set->selection_source)
		return;
	struct tw_data_offer *offer;

	if (device->offer_set &&
	    (surface == device->offer_set->current_surface) &&
	    (device->offer_set->source) == device->source_set)
		return;

	if ((offer = tw_data_device_create_data_offer(device, surface))) {
		device->offer_set = offer;
		offer->current_surface = surface;
		wl_data_device_send_selection(device->resource,
		                              offer->resource);
	}
}

void
tw_data_device_handle_source_destroy(struct tw_data_device *device,
                                     struct tw_data_source *source)
{
	if (device->source_set == source && source->selection_source) {
		wl_data_device_send_selection(device->resource,
		                              NULL);
		device->source_set = NULL;
	}
}

static void
data_device_start_drag(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *source_resource,
                       struct wl_resource *origin_surface,
                       struct wl_resource *icon_source,
                       uint32_t serial)
{
	struct tw_data_device *device = tw_data_device_from_source(resource);
	struct tw_data_source *source =
		tw_data_source_from_resource(source_resource);
	assert(device);

	device->source_set = source;
	source->selection_source = false;
	source->drag_origin_surface = origin_surface;
	source->device = device;

	//TODO: we do not care about the icon_surface role.
	//TODO: for drag to work. Now we would need to have the a custom grab.
	if (!tw_data_source_start_drag(device, device->seat)) {
		device->source_set = NULL;
		source->drag_origin_surface = NULL;
		//creating a data_offer in the entering event.
	}
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
	struct tw_seat *seat = device->seat;

	if (source->actions) {
		wl_resource_post_error(source_resource,
		                       WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
		                       "attempted to set selection on a "
		                       "dnd source");
		return;
	}

	device->source_set = source;
	source->selection_source = true;
	source->device = device;
	if (device->seat->keyboard.focused_surface)
		data_device_selection_data_offer(&device->create_data_offer,
		                              seat->keyboard.focused_surface);
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

	data_source_resource =
		wl_resource_create(client, &wl_data_source_interface,
		                   version, id);
	if (!data_source_resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	if (!(data_source = tw_data_source_create(data_source_resource))) {
		wl_resource_post_no_memory(manager_resource);
		wl_resource_destroy(data_source_resource);
		return;
	}
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
	//install hooks for device.
	device->seat = seat;
	wl_list_init(&device->create_data_offer.link);
	device->create_data_offer.notify = data_device_selection_data_offer;
	wl_signal_add(&seat->focus_signal, &device->create_data_offer);
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

bool
tw_data_device_manager_init(struct tw_data_device_manager *manager,
                            struct wl_display *display)
{
	manager->global = wl_global_create(display,
	                                   &wl_data_device_manager_interface,
	                                   3, manager,
	                                   bind_data_device_manager);
	if (!manager->global)
		return false;
	wl_list_init(&manager->clients);
	wl_list_init(&manager->display_destroy_listener.link);
	manager->display_destroy_listener.notify =
		destroy_data_device_manager;
	wl_display_add_destroy_listener(display,
	                                &manager->display_destroy_listener);
	return true;
}

struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display)
{
	struct tw_data_device_manager *manager =
		&s_tw_data_device_manager;
	if (!tw_data_device_manager_init(manager, display))
		return NULL;

	return manager;
}
