/*
 * data_device.h - taiwins server wl_data_device implementation
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

#ifndef TW_DATA_DEVICE_H
#define TW_DATA_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_DATA_DEVICE_ACCEPT_ACTIONS \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE | \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK

struct tw_data_device;
struct tw_data_source;

struct tw_data_source {
	struct wl_resource *resource;
	struct wl_resource *drag_origin_surface;
	struct tw_data_offer *current_offer;
	/** set at set_selection or start_drag */
	struct tw_data_device *device;
	struct wl_array mimes;
	uint32_t actions;
	uint32_t selected_dnd_action;
	bool selection_source;
	bool accepted;

	struct wl_signal destroy_signal;
};

struct tw_data_offer {
	struct tw_data_source *source;
	struct wl_resource *resource;
	struct wl_resource *current_surface;
	struct wl_listener source_destroy_listener;
	struct wl_list link;
	bool finished;
};

struct tw_data_device {
	struct tw_seat *seat;
	struct wl_resource *resource;
	struct tw_data_source *source_set;
	struct tw_data_offer *offer_set;
	/* data_device would create a new data offer for clients */
	struct wl_listener create_data_offer;
};

struct tw_data_device_manager {
	struct wl_global *global;
	struct wl_list clients;
	struct wl_list devices;

	struct wl_listener display_destroy_listener;
};

struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display);

bool
tw_data_device_manager_init(struct tw_data_device_manager *manager,
                            struct wl_display *display);
/* create the data_offer for the current data_source, invoked ether in
 * wl_data_device.set_selection or wl_data_device.enter. */
struct tw_data_offer *
tw_data_device_create_data_offer(struct tw_data_device *device,
                                 struct wl_resource *surface);
void
tw_data_device_handle_source_destroy(struct tw_data_device *device,
                                     struct tw_data_source *source);
struct tw_data_offer *
tw_data_offer_create(struct wl_resource *resource,
                     struct tw_data_source *source);
struct tw_data_source *
tw_data_source_create(struct wl_resource *resource);

struct tw_data_source *
tw_data_source_from_resource(struct wl_resource *resource);

bool
tw_data_source_start_drag(struct tw_data_device *device,
                          struct tw_seat *seat);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
