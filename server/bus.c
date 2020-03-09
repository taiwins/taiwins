/*
 * bus.c - taiwins server bus implementation
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
#include <libweston/libweston.h>

#include "config.h"
#include "bus.h"


static struct tw_bus {
	struct weston_compositor *compositor;
	struct tw_config *config;

	struct tdbus *dbus;

	struct wl_listener compositor_distroy_listener;
	struct tw_config_component_listener config_component;
} TWbus;


static inline struct tw_bus *
get_bus(void)
{
	return &TWbus;
}

static int
tw_dbus_dispatch_watch(int fd, uint32_t mask, void *data)
{
	struct tw_bus *twbus = get_bus();
	struct tdbus *bus = twbus->dbus;
	void *watch_data = data;

	tdbus_handle_watch(bus, watch_data);

	return 0;
}


static void
tw_bus_add_watch(void *user_data, int unix_fd, struct tdbus *bus,
	uint32_t mask, void *watch_data)
{
	struct wl_event_loop *loop;
	struct tw_bus *twbus = user_data;
	uint32_t flags = 0;
	struct wl_event_source *s;

	loop = wl_display_get_event_loop(twbus->compositor->wl_display);
	if (mask & TDBUS_ENABLED) {
		if (mask & TDBUS_READABLE)
			flags |= WL_EVENT_READABLE;
		if (mask & TDBUS_WRITABLE)
			flags |= WL_EVENT_WRITABLE;
	}

	s = wl_event_loop_add_fd(loop, unix_fd, flags, tw_dbus_dispatch_watch,
	                         watch_data);

	//TODO: notify system it does not work
	if (!s)
		return;

}

static void
tw_bus_ch_watch(void *user_data, int unix_fd, struct tdbus *bus,
                uint32_t mask, void *watch_data)
{
	struct wl_event_source *s;
	uint32_t flags = 0;

	s = tdbus_watch_get_user_data(watch_data);
	if (!s)
		return;

	if (mask & TDBUS_ENABLED) {
		if (flags & TDBUS_READABLE)
			flags |= WL_EVENT_READABLE;
		if (flags & TDBUS_WRITABLE)
			flags |= WL_EVENT_WRITABLE;
	}
	wl_event_source_fd_update(s, flags);


}

static void
tw_bus_rm_watch(void *user_data, int unix_fd, struct tdbus *bus,
                void *watch_data)
{
	struct wl_event_source *s;

	s = tdbus_watch_get_user_data(watch_data);
	if (!s)
		return;

	wl_event_source_remove(s);
}

static void
tw_bus_dispatch(void *data)
{
	struct tw_bus *bus = data;

	tdbus_dispatch_once(bus->dbus);
}

static void
tw_bus_end(struct wl_listener *listener, void *data)
{
	struct tw_bus *bus = container_of(listener, struct tw_bus,
	                                  compositor_distroy_listener);
	struct tdbus *dbus = bus->dbus;

	tdbus_delete(dbus);
}

bool
tw_setup_bus(struct weston_compositor *ec, struct tw_config *config)
{
	struct tw_bus *bus = get_bus();
	struct wl_display *display;
	struct wl_event_loop *loop;

	display = ec->wl_display;
	loop = wl_display_get_event_loop(display);
	bus->compositor = ec;
	bus->config = config;
	bus->dbus = tdbus_new_server(SESSION_BUS, "org.taiwins");

	if (!bus->dbus)
		return NULL;

	wl_list_init(&bus->compositor_distroy_listener.link);
	bus->compositor_distroy_listener.notify = tw_bus_end;
	wl_signal_add(&ec->destroy_signal, &bus->compositor_distroy_listener);

	tdbus_set_nonblock(bus->dbus, bus,
	                   tw_bus_add_watch, tw_bus_ch_watch, tw_bus_rm_watch);
	wl_event_loop_add_idle(loop, tw_bus_dispatch, bus);

	//TODO we would add some methods for dbus
	tdbus_server_add_methods(bus->dbus, "/org/taiwins", 0, NULL);

	return true;
}
