/*
 * data_internal.c - taiwins server wl_data_device internal headers
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

#ifndef TW_DATA_DEVICE_INTERNAL_H
#define TW_DATA_DEVICE_INTERNAL_H

#include <taiwins/objects/data_device.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_DATA_DEVICE_ACCEPT_ACTIONS \
	(WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
	 WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK )

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
