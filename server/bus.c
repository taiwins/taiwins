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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <libweston/libweston.h>
#include <tdbus.h>

#include "bus.h"

static struct tw_bus {
	struct weston_compositor *compositor;
	struct tdbus *dbus;
	struct wl_event_source *source;

	struct wl_listener compositor_distroy_listener;
} s_bus;


static inline struct tw_bus *
get_bus(void)
{
	return &s_bus;
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

		s = wl_event_loop_add_fd(loop, unix_fd, flags,
		                         tw_dbus_dispatch_watch,
		                         watch_data);
		tdbus_watch_set_user_data(watch_data, s);
	}
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

static int
tw_bus_dispatch(int fd, uint32_t mask, void *data)
{
	struct tw_bus *bus = data;

	tdbus_dispatch_once(bus->dbus);

	return 0;
}

static void
tw_bus_end(struct wl_listener *listener, void *data)
{
	struct tw_bus *bus = container_of(listener, struct tw_bus,
	                                  compositor_distroy_listener);
	struct tdbus *dbus = bus->dbus;

	if (bus->source)
		wl_event_source_remove(bus->source);

	tdbus_delete(dbus);
}

static int tw_bus_read_request(const struct tdbus_method_call *call)
{
	char *msg_received = NULL, msg_reply[128];
	const char *invalid_reply = "message not vaild";
	struct tdbus_message *reply;
	struct tdbus *bus = call->bus;

	tdbus_read(call->message, "%s", &msg_received);
	if (!msg_received || strlen(msg_received) > 120)
		goto err_call;

	strcpy(msg_reply, "recv: ");
	strcat(msg_reply, msg_received);
	free(msg_received);

	reply = tdbus_reply_method(call->message, NULL);
	tdbus_write(reply, "%s", msg_reply);
	tdbus_send_message(bus, reply);

	return 0;
err_call:
	if (msg_received)
		free(msg_received);
	reply = tdbus_reply_method(call->message, invalid_reply);
	tdbus_write(reply, "%s", invalid_reply);
	tdbus_send_message(bus, reply);
	return 0;
}

static struct tdbus_call_answer tw_bus_answer = {
	.interface = "org.taiwins.example",
	.method = "Hello",
	.in_signature = "s",
	.out_signature = "s",
	.reader = tw_bus_read_request,
};

struct tw_bus *
tw_setup_bus(struct weston_compositor *ec)
{
	int fd;
	struct tw_bus *bus = get_bus();
	struct wl_display *display;
	struct wl_event_loop *loop;

	display = ec->wl_display;
	loop = wl_display_get_event_loop(display);
	bus->compositor = ec;
	bus->dbus = tdbus_new_server(SESSION_BUS, "org.taiwins");

	wl_list_init(&bus->compositor_distroy_listener.link);
	bus->compositor_distroy_listener.notify = tw_bus_end;
	wl_signal_add(&ec->destroy_signal, &bus->compositor_distroy_listener);

	if (!bus->dbus)
		return NULL;

	/* idle events cannot reschedule themselves */
	fd = eventfd(0, EFD_CLOEXEC);
	if (fd < 0)
		return NULL;
	bus->source = wl_event_loop_add_fd(loop, fd, 0, tw_bus_dispatch, bus);
	if (!bus->source) {
		tw_bus_end(&bus->compositor_distroy_listener, bus);
		return NULL;
	}
	wl_event_source_check(bus->source);

	tdbus_set_nonblock(bus->dbus, bus,
	                   tw_bus_add_watch, tw_bus_ch_watch, tw_bus_rm_watch);

	tdbus_server_add_methods(bus->dbus, "/org/taiwins", 1, &tw_bus_answer);

	return &s_bus;
}
