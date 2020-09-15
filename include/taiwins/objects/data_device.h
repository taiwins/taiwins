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

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
