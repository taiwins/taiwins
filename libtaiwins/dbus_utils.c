/*
 * dbus_utils.c - taiwins server dbus utils
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

#include <unistd.h>
#include <sys/eventfd.h>
#include <tdbus.h>
#include <wayland-server-core.h>
#include <taiwins/objects/logger.h>

#include "utils.h"

static int
tw_dbus_dispatch_watch(int fd, uint32_t mask, void *data)
{
	void *watch_data = data;
	tdbus_handle_watch(watch_data);
	return 0;
}

static void
tw_bus_add_watch(void *user_data, int unix_fd, struct tdbus *bus,
                 uint32_t mask, void *watch_data)
{
	struct wl_display *display = user_data;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	uint32_t flags = 0;
	struct wl_event_source *s;

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
tw_bus_ch_watch(void *user_data,int unix_fd, struct tdbus *bus, uint32_t mask,
                void *watch_data)
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
tw_dbus_dispatch_timeout(void *data)
{
	tdbus_handle_timeout(data);
	return 0;
}

static void
tw_bus_add_timeout(void *user_data, int interval, bool enabled,
                   struct tdbus *bus, void *timeout)
{
	struct wl_display *display = user_data;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	struct wl_event_source *s;

	if (enabled) {
		s = wl_event_loop_add_timer(loop, tw_dbus_dispatch_timeout,
		                            timeout);
		if (!s)
			return;
		wl_event_source_timer_update(s, interval);
		tdbus_timeout_set_user_data(timeout, s);
	}
}

static void
tw_bus_ch_timeout(void *user_data, int interval, struct tdbus *bus,
                  void *timeout)
{
	struct wl_event_source *s;

	s = tdbus_timeout_get_user_data(timeout);
	if (!s)
		return;
	wl_event_source_timer_update(s, interval);
}

static void
tw_bus_rm_timeout(void *user_data, struct tdbus *bus, void *timeout)
{
	struct wl_event_source *s = tdbus_timeout_get_user_data(timeout);

        if (!s) return;
        wl_event_source_remove(s);
}

static int
tw_bus_dispatch(int fd, uint32_t mask, void *data)
{
	struct tdbus *bus = data;

	tdbus_dispatch_once(bus);
	return 0;
}

static void
tw_handle_tdbus_log(enum tdbus_log_level level, const char *fmt, va_list ap)
{
	enum TW_LOG_LEVEL lvl = TW_LOG_INFO;

        switch (level) {
	case TDBUS_LOG_DBUG:
		lvl = TW_LOG_DBUG;
		break;
	case TDBUS_LOG_INFO:
		lvl = TW_LOG_INFO;
		break;
	case TDBUS_LOG_WARN:
		lvl = TW_LOG_WARN;
		break;
	case TDBUS_LOG_ERRO:
		lvl = TW_LOG_ERRO;
		break;
	}
        tw_logv_level(lvl, fmt, ap);
}

WL_EXPORT struct wl_event_source *
tw_bind_tdbus_for_wl_display(struct tdbus *bus, struct wl_display *display)
{
	int fd;
	struct wl_event_source *src;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	//we need to make a constant idle event, by firstly create a fd event
	//then close it and check the event.
	fd = eventfd(0, EFD_CLOEXEC);
	if (fd < 0)
		return NULL;
	src = wl_event_loop_add_fd(loop, fd, 0, tw_bus_dispatch, bus);
	close(fd);

	if (!src)
		return NULL;

	wl_event_source_check(src);

	tdbus_set_nonblock(bus, display, tw_bus_add_watch, tw_bus_ch_watch,
	                   tw_bus_rm_watch, tw_bus_add_timeout,
	                   tw_bus_ch_timeout, tw_bus_rm_timeout);
	tdbus_set_logger(bus, tw_handle_tdbus_log);
	return src;
}
