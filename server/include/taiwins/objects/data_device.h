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
#include <wayland-server.h>

#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_DATA_DEVICE_ACCEPT_ACTIONS \
	(WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK )

struct tw_data_device;
struct tw_data_source;
struct tw_data_offer;
struct tw_data_drag;

/**
 * @brief tw_data_offer represents multiple wl_data_offers in the clients.
 *
 * the offer tracks its source, will get notified when source is gone.
 */
struct tw_data_offer {
	struct tw_data_source *source;
	struct wl_resource *current_surface;
	struct wl_listener source_destroy_listener;
	struct wl_list resources;
	bool finished;
};

/**
 * @brief tw_data_source represents a wl_data_source for the client
 *
 * the source tracks current offer
 */
struct tw_data_source {
	struct wl_resource *resource;
	/** set at set_selection or start_drag */
	struct wl_array mimes;
	uint32_t actions;
	uint32_t selected_dnd_action;
	bool selection_source;
	bool accepted;

	/**< data offer is embedded in the source available at source
	 * creation */
	struct tw_data_offer offer;

	struct wl_listener device_destroy_listener;
	struct wl_signal destroy_signal;
};

struct tw_data_drag {
	struct tw_data_source *source;
	struct wl_resource *dest_device_resource;
	struct tw_seat_pointer_grab pointer_grab;
	struct tw_seat_keyboard_grab keyboard_grab;
	struct wl_listener source_destroy_listener;
};

/**
 * @brief tw_data_device represents a seat
 *
 * the data_device tracks current data source, will be notified if source is
 * gone.
 */
struct tw_data_device {
	struct wl_list link;
	struct tw_seat *seat;
	struct wl_list clients;
	struct tw_data_source *source_set;
	/**< the drag for this device, since a seat has one drag at a time  */
	struct tw_data_drag drag;

	struct wl_listener source_destroy;
	struct wl_listener create_data_offer;
	struct wl_listener seat_destroy;
};

struct tw_data_device_manager {
	struct wl_global *global;
	struct wl_list devices;

	struct wl_listener display_destroy_listener;
};

struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display);

bool
tw_data_device_manager_init(struct tw_data_device_manager *manager,
                            struct wl_display *display);

/**
 * @brief create a data_offer for this surface.
 */
struct wl_resource *
tw_data_device_create_data_offer(struct wl_resource *device_resource,
                                 struct tw_data_source *source);
struct wl_resource *
tw_data_device_find_client(struct tw_data_device *device,
                           struct wl_resource *r);
void
tw_data_offer_init(struct tw_data_offer *offer,
                   struct tw_data_source *source);
void
tw_data_offer_add_resource(struct tw_data_offer *offer,
                           struct wl_resource *offer_resource,
                           struct wl_resource *device_resource);
void
tw_data_offer_set_source_actions(struct tw_data_offer *offer,
                                 uint32_t dnd_actions);
struct tw_data_source *
tw_data_source_create(struct wl_client *client, uint32_t id,
                      uint32_t version);

struct tw_data_source *
tw_data_source_from_resource(struct wl_resource *resource);

bool
tw_data_source_start_drag(struct tw_data_drag *drag,
                          struct wl_resource *device_resource,
                          struct tw_data_source *source, struct tw_seat *seat);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
