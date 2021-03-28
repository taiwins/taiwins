/*
 * xwayland.c - taiwins compositor xwayland implementation
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

#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <taiwins/engine.h>
#include <taiwins/backend.h>
#include <taiwins/xwayland.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/data_device.h>
#include "xwayland.h"

static void
notify_xwayland_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xwayland *xwayland =
		wl_container_of(listener, xwayland, listeners.display_destroy);
	wl_list_remove(&listener->link);
	wl_list_remove(&xwayland->listeners.xserver_created.link);
	wl_list_remove(&xwayland->listeners.seat_focused.link);

	tw_xserver_fini(xwayland->server);
	xwayland->server = NULL;
	xwayland->engine = NULL;
	xwayland->desktop_manager = NULL;
}

static void
notify_xwayland_seat_focused(struct wl_listener *listener, void *data)
{
	struct tw_xwayland *xwayland =
		wl_container_of(listener, xwayland, listeners.seat_focused);
	struct tw_data_device_manager *device_manager =
		&xwayland->engine->data_device_manager;
	struct tw_engine_seat *seat = data;
	struct tw_data_device *device = NULL;

	if (!xwayland->server->wm || !seat)
		return;
	device = tw_data_device_find_create(device_manager, seat->tw_seat);
	if (device)
		tw_xserver_set_seat(xwayland->server, device);
}

static void
notify_xwayland_create_xwm(struct wl_listener *listener, void *data)
{
	struct tw_xwayland *xwayland =
		wl_container_of(listener, xwayland, listeners.xserver_created);
	struct tw_desktop_manager *manager = xwayland->desktop_manager;
	//TODO compositor may moved to other places
	struct tw_compositor *compositor =
		&xwayland->engine->compositor_manager;

	tw_xserver_create_xwindow_manager(xwayland->server,
	                                  manager, compositor);
	notify_xwayland_seat_focused(&xwayland->listeners.seat_focused,
	                             xwayland->engine->focused_seat);
}


static bool
tw_xwayland_init(struct tw_xwayland *xwayland, struct tw_engine *engine,
                 struct tw_desktop_manager *desktop_manager,
                 bool lazy)
{
	struct wl_display *display = engine->display;
	struct tw_backend *backend = engine->backend;

	if (!(xwayland->desktop_manager = desktop_manager))
		return false;
	if (!(xwayland->server = tw_xserver_create_global(display, lazy)))
		return false;
	xwayland->engine = engine;

	tw_signal_setup_listener(&backend->signals.stop,
	                         &xwayland->listeners.display_destroy,
	                         notify_xwayland_display_destroy);
	tw_signal_setup_listener(&xwayland->server->signals.ready,
	                         &xwayland->listeners.xserver_created,
	                         notify_xwayland_create_xwm);
	tw_signal_setup_listener(&engine->signals.seat_focused,
	                         &xwayland->listeners.seat_focused,
	                         notify_xwayland_seat_focused);
	return true;
}

struct tw_xwayland *
tw_xwayland_create_global(struct tw_engine *engine,
                          struct tw_desktop_manager *desktop_manager,
                          bool lazy)
{
	static struct tw_xwayland xwayland = {0};

	assert(!xwayland.engine);
	assert(!xwayland.server);

	if (!tw_xwayland_init(&xwayland, engine, desktop_manager, lazy))
		return false;
	return &xwayland;
}
