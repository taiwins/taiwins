/*
 * xwayland.h - taiwins compositor xwayland header
 *
 * Copyright (c) 2019 Xichen Zhou
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

#ifndef TW_COMPOSITOR_XWAYLAND_H
#define TW_COMPOSITOR_XWAYLAND_H

#include <wayland-server-core.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/xwayland.h>
#include <taiwins/engine.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xwayland {
	struct tw_xserver *server;
	struct tw_xwm *xwm;
	struct tw_engine *engine;
	struct tw_desktop_manager *desktop_manager;

	struct {
		struct wl_listener display_destroy;
		struct wl_listener xserver_created;
		struct wl_listener seat_focused; /**< set the data_device */
	} listeners;
};


struct tw_xwayland *
tw_xwayland_create_global(struct tw_engine *engine,
                          struct tw_desktop_manager *desktop_manager,
                          bool lazy);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
