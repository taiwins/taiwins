/*
 * idle_inhibit.h - taiwins server idle-inhibit header
 *
 * Copyright (c) 2021 Xichen Zhou
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

#ifndef TW_IDLE_INHIBIT_H
#define TW_IDLE_INHIBIT_H

#include <wayland-server.h>
#include "surface.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_idle_inhibit_manager {
	struct wl_global *global;
	/** emit when inhibitor added or removed */
	struct wl_signal inhibitor_request;
	struct wl_list inhibitors; /**< current active inhibitors */

	struct wl_listener display_destroy_listener;
};


bool
tw_idle_inhibit_manager_init(struct tw_idle_inhibit_manager *mgr,
                             struct wl_display *display);
struct tw_idle_inhibit_manager *
tw_idle_inhibit_manager_create_global(struct wl_display *display);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
