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
#include <wayland-server.h>
#include <wayland-util.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/cursor.h>
#include <taiwins/objects/surface.h>

#include "data_internal.h"

static struct tw_data_device_manager s_tw_data_device_manager;

static struct tw_data_device *
tw_data_device_from_source(struct wl_resource *resource);

struct wl_resource *
tw_data_device_find_client(struct tw_data_device *device,
                           struct wl_resource *r)
{
	struct wl_resource *dev_res;
	wl_resource_for_each(dev_res, &device->clients)
		if (wl_resource_get_client(dev_res) ==
		    wl_resource_get_client(r))
			return dev_res;
	return NULL;
}

struct wl_resource *
tw_data_device_create_data_offer(struct wl_resource *device_resource,
                                 struct tw_data_source *source)
{
	struct wl_resource *offer_resource;
	struct wl_client *client;
	uint32_t version;

	client = wl_resource_get_client(device_resource);
	version = wl_resource_get_version(device_resource);

	if (!(offer_resource = wl_resource_create(client,
	                                          &wl_data_offer_interface,
	                                          version, 0))) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	tw_data_offer_add_resource(&source->offer,
	                           offer_resource, device_resource);
	return offer_resource;
}

static void
notify_device_selection_data_offer(struct wl_listener *listener, void *data)
{
	struct wl_resource *resource;
	struct wl_resource *offer;
	struct wl_resource *surface = data;
	struct tw_data_device *device =
		wl_container_of(listener, device, create_data_offer);

	if (!device->source_set || !device->source_set->selection_source)
		return;
	//send the data offers
	wl_resource_for_each(resource, &device->clients) {
		if (!tw_match_wl_resource_client(surface, resource))
			continue;
		offer = tw_data_device_create_data_offer(resource,
		                                         device->source_set);
		wl_data_device_send_selection(resource, offer);
	}
}

static void
data_device_start_drag(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *source_resource,
                       struct wl_resource *surface_resource,
                       struct wl_resource *icon_source,
                       uint32_t serial)
{
	float sx, sy;
	struct tw_data_device *device = tw_data_device_from_source(resource);
	struct tw_cursor *cursor = device->seat->cursor;
	struct tw_data_source *source =
		tw_data_source_from_resource(source_resource);
	struct tw_surface *surface =
		tw_surface_from_resource(surface_resource);

	source->selection_source = false;
	//TODO: match the serial against pointer or touch for the grab.
	if (tw_data_source_start_drag(&device->drag, resource, source,
	                              device->seat)) {
		//we need to trigger a enter event
		tw_surface_to_local_pos(surface, cursor->x, cursor->y,
		                        &sx, &sy);
		tw_pointer_notify_enter(&device->seat->pointer,
		                        surface_resource, sx, sy);
	}
}

static void
notify_device_source_destroy(struct wl_listener *listener, void *data)
{
	struct tw_data_device *device =
		wl_container_of(listener, device, source_destroy);

	tw_reset_wl_list(&device->source_destroy.link);
	device->source_set = NULL;
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

	if (device->source_set == source)
		return;

	if (source->actions) {
		wl_resource_post_error(source_resource,
		                       WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
		                       "attempted to set selection on a "
		                       "dnd source");
		return;
	}
	//reset the current source, we need to notify the source it is not valid
	//anymore
	if (device->source_set) {
		wl_data_source_send_cancelled(device->source_set->resource);
		tw_reset_wl_list(&device->source_destroy.link);
		device->source_set = NULL;
	}

	device->source_set = source;
	source->selection_source = true;
	tw_signal_setup_listener(&source->destroy_signal,
	                         &device->source_destroy,
	                         notify_device_source_destroy);

	if (device->seat->keyboard.focused_surface)
		notify_device_selection_data_offer(&device->create_data_offer,
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
	wl_list_remove(wl_resource_get_link(resource));
}

static void
tw_data_device_destroy(struct tw_data_device *device)
{
	struct wl_resource *resource, *tmp;
	tw_reset_wl_list(&device->seat_destroy.link);
	tw_reset_wl_list(&device->create_data_offer.link);
	wl_list_remove(&device->link);

	wl_resource_for_each_safe(resource, tmp, &device->clients)
		wl_resource_destroy(resource);

	free(device);
}

static void
notify_data_device_seat_destroy(struct wl_listener *listener, void *data)
{
	struct tw_data_device *device =
		wl_container_of(listener, device, seat_destroy);

	tw_data_device_destroy(device);
}

static struct tw_data_device *
tw_data_device_find_create(struct tw_data_device_manager *manager,
                           struct tw_seat *seat)
{
	struct tw_data_device *device;
	wl_list_for_each(device, &manager->devices, link)
		if (device->seat == seat)
			return device;

	device = calloc(1, sizeof(*device));
	if (!device)
		return NULL;
	device->seat = seat;
	wl_list_init(&device->link);
	wl_list_init(&device->clients);
	wl_list_init(&device->source_destroy.link);
	wl_list_insert(manager->devices.prev, &device->link);
	tw_signal_setup_listener(&seat->focus_signal,
	                         &device->create_data_offer,
	                         notify_device_selection_data_offer);
	tw_signal_setup_listener(&seat->destroy_signal,
	                         &device->seat_destroy,
	                         notify_data_device_seat_destroy);
	return device;
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
	uint32_t version = wl_resource_get_version(manager_resource);

	if (!(data_source = tw_data_source_create(client, id, version))) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
}

static void
get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource)
{
	struct wl_resource *device_resource = NULL;
	struct tw_data_device *device;
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);
	struct tw_data_device_manager *manager =
		wl_resource_get_user_data(manager_resource);

	assert(seat);
	device = tw_data_device_find_create(manager, seat);
	assert(device);

	device_resource = wl_resource_create(client, &wl_data_device_interface,
	                                     version, id);
	if (!device_resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_list_insert(device->clients.prev,
	               wl_resource_get_link(device_resource));

	wl_resource_set_implementation(device_resource, &data_device_impl,
	                               device, destroy_data_device_resource);
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
notify_data_device_manager_display_destroy(struct wl_listener *listener,
                                           void *data)
{
	struct tw_data_device_manager *manager =
		wl_container_of(listener, manager, display_destroy_listener);
	struct tw_data_device *device, *tmp;
	wl_list_for_each_safe(device, tmp, &manager->devices, link)
		tw_data_device_destroy(device);

	wl_global_destroy(manager->global);
}

WL_EXPORT bool
tw_data_device_manager_init(struct tw_data_device_manager *manager,
                            struct wl_display *display)
{
	manager->global = wl_global_create(display,
	                                   &wl_data_device_manager_interface,
	                                   3, manager,
	                                   bind_data_device_manager);
	if (!manager->global)
		return false;
	wl_list_init(&manager->devices);
	wl_list_init(&manager->display_destroy_listener.link);
	manager->display_destroy_listener.notify =
		notify_data_device_manager_display_destroy;
	wl_display_add_destroy_listener(display,
	                                &manager->display_destroy_listener);
	return true;
}

WL_EXPORT struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display)
{
	struct tw_data_device_manager *manager =
		&s_tw_data_device_manager;
	if (!tw_data_device_manager_init(manager, display))
		return NULL;

	return manager;
}
