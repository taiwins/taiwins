#include <sys/epoll.h>

#include "event_queue.h"
#include "shell.h"
#include "tdbus.h"

static int dispatch_watch(struct tw_event *event, int fd)
{
	tdbus_handle_watch((struct tdbus *)event->arg.o, event->data);
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

	if (mask & TDBUS_READABLE)
		epoll_mask |= EPOLLIN;
	if (mask & TDBUS_WRITABLE)
		epoll_mask |= EPOLLOUT;

	tw_event_queue_add_source(queue, fd, &event, epoll_mask);
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

	if (mask & TDBUS_READABLE)
		epoll_mask |= EPOLLIN;
	if (mask & TDBUS_WRITABLE)
		epoll_mask |= EPOLLOUT;

	tw_event_queue_modify_source(queue, fd, &event, mask);
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
	                   shell_bus_remove_watch);

	tdbus_set_nonblock(shell->session_bus, shell,
	                   shell_bus_add_watch, shell_bus_change_watch,
	                   shell_bus_remove_watch);

	session_event.data = shell->session_bus;
	system_event.data = shell->system_bus;
	session_event.cb = dispatch_tdbus;
	system_event.cb  = dispatch_tdbus;

	tw_event_queue_add_idle(&shell->globals.event_queue, &session_event);
	tw_event_queue_add_idle(&shell->globals.event_queue, &system_event);
}

void shell_tdbus_end(struct desktop_shell *shell)
{
	tdbus_delete(shell->system_bus);
	tdbus_delete(shell->session_bus);
}
