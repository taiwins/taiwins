/*
 * taiwins.h - taiwins server shared header
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

#ifndef TAIWINS_H
#define TAIWINS_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctypes/helpers.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>

#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_data_device.h>
#include <objects/logger.h>
#include <objects/layers.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include <objects/subprocess.h>
#include <objects/dmabuf.h>
#include <objects/seat.h>
#include <objects/data_device.h>

#include "backend/backend.h"
#include "desktop/shell_internal.h"
#include "desktop/xdg.h"
#include "input.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_server {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */

	/* globals */
	struct wlr_backend *wlr_backend;
	struct wlr_renderer *wlr_renderer;
	struct tw_backend *backend;
	struct tw_bindings *binding_state;
	struct tw_shell *tw_shell;
	struct tw_xdg *tw_xdg;

	/* seats */
	struct tw_seat_events seat_events[8];
	struct wl_listener seat_add;
	struct wl_listener seat_remove;
	/* render */
	struct wl_listener output_frame;
	struct wl_listener surface_created;
};

bool
tw_server_init(struct tw_server *server, struct wl_display *display);

void
tw_server_build_surface_list(struct tw_server *server);

void
tw_server_stack_damage(struct tw_server *server);

/******************************************************************************
 * util functions
 *****************************************************************************/

bool
tw_set_wl_surface(struct wl_client *client,
                  struct wl_resource *resource,
                  struct wl_resource *surface,
                  struct wl_resource *output,
                  struct wl_listener *surface_destroy_listener);

/******************************************************************************
 * libweston interface functions
 *****************************************************************************/

void *
tw_load_weston_module(const char *name, const char *entrypoint);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
