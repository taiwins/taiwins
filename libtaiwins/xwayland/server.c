/*
 * server.c - taiwins xwayland server
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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/xwayland.h>
#include <wayland-util.h>

#define LOCK_FMT "/tmp/.X%d-lock"
#define SOCKET_DIR "/tmp/.X11-unix"
#define SOCKET_FMT "/tmp/.X11-unix/X%d"
#define SOCKET_FMT_ABS "%c/tmp/.X11-unix/X%d"

static bool
xserver_start_lazy(struct tw_xserver *xserver)
{
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xserver->wl_display);
	(void)loop;
	return true;
}

/******************************************************************************
 * xserver socket
 *****************************************************************************/

#ifdef __linux__

static int
bind_abstract_socket(int display)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX, };
	size_t path_size;
	int fd, size;

	path_size = snprintf(addr.sun_path, sizeof(addr.sun_path),
	                     SOCKET_FMT_ABS, 0, display);

	//or we set_cloexec ourselves
	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	size = offsetof(struct sockaddr_un, sun_path) + path_size;

	if (bind(fd, (struct sockaddr *)&addr, size) < 0) {
		tw_log_level(TW_LOG_WARN, "failed to bind to @%s: %s",
		             addr.sun_path+1, strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, 1) < 0) {
		tw_logl_level(TW_LOG_WARN, "failed to listen to @%s: %s",
		              addr.sun_path+1, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

#endif

static int
bind_unix_socket(int display)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX, };
	size_t path_size;
	int fd, size;

	path_size = snprintf(addr.sun_path, sizeof(addr.sun_path),
	                     SOCKET_FMT, display) + 1;
	size = offsetof(struct sockaddr_un, sun_path) + path_size;
	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (!fd)
		return -1;
	if (bind(fd, (struct sockaddr *)&addr, size) < 0) {
		tw_logl_level(TW_LOG_WARN, "failed to bind to %s: %s\n",
		              addr.sun_path, strerror(errno));
		unlink(addr.sun_path);
		close(fd);
		return -1;
	}
	if (listen(fd, 1) < 0) {
		unlink(addr.sun_path);
		close(fd);
		return -1;
	}
	return fd;
}

static bool
xserver_open_sockets(struct tw_xserver *server, int display)
{
	mkdir(SOCKET_DIR, 0777);

#ifdef __linux__
	server->abstract_fd = bind_abstract_socket(display);
	if (server->abstract_fd < 0)
		return false;
#endif
	server->unix_fd = bind_unix_socket(display);
	if (server->unix_fd < 0) {
#ifdef __linux__
		close(server->abstract_fd);
#endif
		return false;
	}
	return true;
}

static bool
xserver_connect_display(struct tw_xserver *xserver,
                        struct wl_display *wl_display)
{
	int lock_fd, display = -1;
	char lock_name[128];
	int mode = O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL;
	//find an available slot for x11-socket
	for (display = 0; display < 32; display++) {
		//find a empty socket(<32) that we can use, this is by simply
		snprintf(lock_name, sizeof(lock_name), LOCK_FMT, display);

		//already exists
		if ((lock_fd = open(lock_name, O_RDONLY | O_CLOEXEC)) >= 0) {
			close(lock_fd);
			continue;
		}
		if ((lock_fd = open(lock_name, mode, 0444)) >= 0) {
			//the lockfile contains single line of the pid of the
			//xserver
			char pid[12];

			if (!xserver_open_sockets(xserver, display)) {
				//delete it
				unlink(lock_name);
				close(lock_fd);
				continue;
			}

			snprintf(pid, sizeof(pid), "%10d", getpid());
			if (write(lock_fd, pid, sizeof(pid) - 1) !=
			    sizeof(pid)-1) {
				//delete it
				unlink(lock_name);
				close(lock_fd);
				continue;
			}
			close(lock_fd);
			break;
		} else {
			continue;
		}
	}

	if (display >= 32) {
		tw_logl_level(TW_LOG_WARN, "No display available");
		xserver->abstract_fd = -1;
		xserver->unix_fd = -1;
		return false;
	}

	xserver->display = display;
	snprintf(xserver->name, sizeof(xserver->name), ":%d",
	         xserver->display);

	return true;
}

static void
xserver_finish_display(struct tw_xserver *xserver)
{
	wl_list_remove(&xserver->listeners.display_destroy.link);

	if (xserver->display == -1)
		return;
	if (xserver->abstract_fd >= 0)
		close(xserver->abstract_fd);
	if (xserver->unix_fd >= 0)
		close(xserver->abstract_fd);

	xserver->abstract_fd = -1;
	xserver->unix_fd = -1;
	xserver->display = -1;
	xserver->name[0] = 0;
}

static void
notify_xserver_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xserver *server =
		wl_container_of(listener, server, listeners.display_destroy);
	//finish display?
}

bool
tw_xserver_init(struct tw_xserver *server, struct wl_display *display)
{
	server->wl_display = display;
	server->abstract_fd = -1;
	server->unix_fd = -1;

	wl_signal_init(&server->events.destroy);
	wl_signal_init(&server->events.ready);

	if (!xserver_connect_display(server, display))
		return false;

	tw_set_display_destroy_listener(display,
	                                &server->listeners.display_destroy,
	                                notify_xserver_display_destroy);

	if (xserver_start_lazy(server)) {
		return true;
	} else {
		xserver_finish_display(server);
		return false;
	}
}

struct tw_xserver *
tw_xserver_create_global(struct wl_display *display)
{
	static struct tw_xserver s_server = {0};


	if (tw_xserver_init(&s_server, display))
		return &s_server;
	else
		return NULL;
}
