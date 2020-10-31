/*
 * shell_bus.c - taiwins client shell dbus interface
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

#include <stdint.h>
#include <sys/epoll.h>
#include <twclient/event_queue.h>
#include <ctypes/helpers.h>
#include <unistd.h>
#include <tdbus.h>

#include "shell.h"

static int
dispatch_watch(struct tw_event *event, UNUSED_ARG(int fd))
{
	tdbus_handle_watch(event->data);
	return TW_EVENT_NOOP;
}

static void
shell_bus_add_watch(void *user_data, int fd, struct tdbus *bus,
                    uint32_t mask, void *watch_data)
{
	struct tw_event event;
	struct desktop_shell *shell = user_data;
	struct tw_event_queue *queue = &shell->globals.event_queue;
	int epoll_mask = 0;

	event.data = watch_data;
	event.cb = dispatch_watch;
	event.arg.o = (void *)bus;

        if (mask & TDBUS_ENABLED) {
	        if (mask & TDBUS_READABLE)
		        epoll_mask |= EPOLLIN;
	        if (mask & TDBUS_WRITABLE)
		        epoll_mask |= EPOLLOUT;

	        tw_event_queue_add_source(queue, fd, &event, epoll_mask);
        }
}

static void
shell_bus_change_watch(void *user_data, int fd, struct tdbus *bus,
                       uint32_t mask, void *watch_data)
{
	struct tw_event event;
	struct desktop_shell *shell = user_data;
	struct tw_event_queue *queue = &shell->globals.event_queue;
	int epoll_mask = 0;

	event.data = watch_data;
	event.cb = dispatch_watch;
	event.arg.o = (void *)bus;

        if (mask & TDBUS_ENABLED) {
	        if (mask & TDBUS_READABLE)
		        epoll_mask |= EPOLLIN;
	        if (mask & TDBUS_WRITABLE)
		        epoll_mask |= EPOLLOUT;

	        tw_event_queue_modify_source(queue, fd, &event, mask);
        }
}

static void
shell_bus_remove_watch(void *user_data, int fd, struct tdbus *bus,
                       void *watch_data)
{
	struct desktop_shell *shell = user_data;
	struct tw_event_queue *queue = &shell->globals.event_queue;

	tw_event_queue_remove_source(queue, fd);
}

static int
dispatch_timeout(struct tw_event *event, int fd)
{
	//timeout only happen once
	tdbus_handle_timeout(event->data);
	return TW_EVENT_DEL;
}

static void
shell_bus_add_timeout(void *user_data, int interval, bool enabled,
                      struct tdbus *bus, void *timeout)
{
	struct tw_event event;
	struct desktop_shell *shell = user_data;
        struct tw_event_queue *queue = &shell->globals.event_queue;
        struct itimerspec timespec = {
	        .it_value = {
		        .tv_sec = 0,
		        .tv_nsec = interval * 1000,
	        },
	        .it_interval = {
		        .tv_sec = 0,
		        .tv_nsec = interval * 1000,
	        },
        };
        intptr_t timerfd;

        event.data = timeout;
        event.cb = dispatch_timeout;
        event.arg.o = (void *)bus;

        if (enabled) {
	        timerfd = tw_event_queue_add_timer(queue, &timespec, &event);
	        tdbus_timeout_set_user_data(timeout, (void *)timerfd);
        }
}

static void
shell_bus_change_timeout(void *user_data, int interval, struct tdbus *bus,
                         void *timeout)
{
	intptr_t timerfd = (intptr_t)tdbus_timeout_get_user_data(timeout);
	//this would work?
	tdbus_timeout_reset_timer(timeout, timerfd);
}

static void
shell_bus_close_timeout(void *user_data, struct tdbus *bus, void *timeout)
{
	intptr_t timerfd = (intptr_t)tdbus_timeout_get_user_data(timeout);
	struct desktop_shell *shell = user_data;
        struct tw_event_queue *queue = &shell->globals.event_queue;

        tw_event_queue_remove_source(queue, timerfd);
}

static int
dispatch_tdbus(struct tw_event *event, int fd)
{
	tdbus_dispatch_once(event->data);
	return TW_EVENT_NOOP;
}

void
shell_tdbus_init(struct desktop_shell *shell)
{
	struct tw_event session_event, system_event;

	shell->system_bus = tdbus_new(SYSTEM_BUS);
	shell->session_bus = tdbus_new(SESSION_BUS);

	tdbus_set_nonblock(shell->system_bus, shell,
	                   shell_bus_add_watch, shell_bus_change_watch,
	                   shell_bus_remove_watch,
	                   shell_bus_add_timeout, shell_bus_change_timeout,
	                   shell_bus_close_timeout);

	tdbus_set_nonblock(shell->session_bus, shell,
	                   shell_bus_add_watch, shell_bus_change_watch,
	                   shell_bus_remove_watch,
	                   shell_bus_add_timeout, shell_bus_change_timeout,
	                   shell_bus_close_timeout);

	session_event.data = shell->session_bus;
	system_event.data = shell->system_bus;
	session_event.cb = dispatch_tdbus;
	system_event.cb  = dispatch_tdbus;

	tw_event_queue_add_idle(&shell->globals.event_queue, &session_event);
	tw_event_queue_add_idle(&shell->globals.event_queue, &system_event);
}

void
shell_tdbus_end(struct desktop_shell *shell)
{
	tdbus_delete(shell->system_bus);
	tdbus_delete(shell->session_bus);
}
