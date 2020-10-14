/*
 * protocol.c - taiwins server wayland backend wl_protocols
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

#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>
#include <wayland-xdg-shell-client-protocol.h>
#include <taiwins/objects/logger.h>
#include "internal.h"

static void
handle_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                    uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = handle_wm_base_ping,
};


static void
handle_registry_global(void *data, struct wl_registry *registry, uint32_t id,
                       const char *name, uint32_t version)
{
	struct tw_wl_backend *wl = data;

	tw_logl("Binding wayland global: %s v%d", name, version);
	if (strcmp(name, wl_compositor_interface.name) == 0) {
		wl->globals.compositor =
			wl_registry_bind(registry, id,
			                 &wl_compositor_interface, version);
	} else if (strcmp(name, wl_seat_interface.name) == 0) {
		struct tw_wl_seat *seat = tw_wl_handle_new_seat(wl, registry,
		                                                id, version);
		if (seat)
			wl_list_insert(wl->seats.prev, &seat->link);
	} else if (strcmp(name, xdg_wm_base_interface.name) == 0) {
		wl->globals.wm_base =
			wl_registry_bind(registry, id, &xdg_wm_base_interface,
			                 version);
		xdg_wm_base_add_listener(wl->globals.wm_base,
		                         &wm_base_listener, wl);
	}
}

static void
handle_registry_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{

}


static const struct wl_registry_listener registry_listener = {
	.global = handle_registry_global,
	.global_remove = handle_registry_global_remove,
};



void
tw_wl_bind_wl_registry(struct tw_wl_backend *wl)
{
        wl_registry_add_listener(wl->registry, &registry_listener, wl);
}
