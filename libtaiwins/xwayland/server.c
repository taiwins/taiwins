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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/xwayland.h>
#include <wayland-util.h>

#define LOCK_FMT "/tmp/.X%d-lock"
#define SOCKET_DIR "/tmp/.X11-unix"
#define SOCKET_FMT "/tmp/.X11-unix/X%d"
#define SOCKET_FMT_ABS "%c/tmp/.X11-unix/X%d"
#define XSERVER_PATH "Xwayland"

static inline
void secure_close(int *fd)
{
	if (*fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

static int
handle_ready(int signumber, void *data)
{
	struct tw_xserver *xserver = data;

	wl_event_source_remove(xserver->sigusr1_source);
	xserver->sigusr1_source = NULL;
	wl_signal_emit(&xserver->signals.ready, &xserver);
	return 1;
}

static int
handle_xwayland_fork(pid_t pid, struct tw_subprocess *chld)
{
	int devnull = -1;
	struct tw_xserver *xserver = wl_container_of(chld, xserver, process);

	if (pid == 0) {
		/* X server send SIGUSR1 to the parent when it's done
		 * initialization */
		signal(SIGUSR1, SIG_IGN);
		//redirect the xwayland output
		devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
	} else {
		close(xserver->wms[1]);
		xserver->wms[1] = -1;
		xserver->pid = pid;
	}
	return 0;
}

static int
handle_xwayland_exec(const char *path, struct tw_subprocess *chld)
{
	int fd;
	char abstract_fd_str[12], unix_fd_str[12], wm_fd_str[12];
	char display[12];

	struct tw_xserver *xserver =
		wl_container_of(chld, xserver, process);
	snprintf(display, sizeof(display), ":%d", xserver->display);

	//dup fd because they are cloexec
	fd = dup(xserver->abstract_fd);
	if (fd < 0)
		return -1;
	snprintf(abstract_fd_str, sizeof abstract_fd_str, "%d", fd);
	fd = dup(xserver->unix_fd);
	if (fd < 0)
		return -1;
	snprintf(unix_fd_str, sizeof unix_fd_str, "%d", fd);
	fd = dup(xserver->wms[1]);
	if (fd < 0)
		return -1;
	snprintf(wm_fd_str, sizeof wm_fd_str, "%d", fd);

	if (execlp(path, path, display,
	           "-rootless",
#ifdef __linux__
	           "-listen", abstract_fd_str,
#endif
	           "-listen", unix_fd_str,
	           "-wm", wm_fd_str,
	           "-terminate", NULL) < 0) {
		fprintf(stderr, "exec of '%s %s -rootless "
		        "-listen %s -listen %s -wm %s "
		        "-terminate' failed: %s\n",
		        path, display,
		        abstract_fd_str, unix_fd_str, wm_fd_str,
		        strerror(errno));
		return -1;
	}
	return 0;
}

static void
xserver_finish_process(struct tw_xserver *xserver)
{
	if (xserver->abstract_source) {
		wl_event_source_remove(xserver->abstract_source);
		xserver->abstract_source = NULL;
	}
	if (xserver->unix_source) {
		wl_event_source_remove(xserver->unix_source);
		xserver->unix_source = NULL;
	}
	if (xserver->sigusr1_source) {
		wl_event_source_remove(xserver->sigusr1_source);
		xserver->sigusr1_source = NULL;
	}
	if (xserver->client) {
		tw_reset_wl_list(&xserver->process.link);
		wl_client_destroy(xserver->client);
	}

	secure_close(&xserver->wms[0]);
	secure_close(&xserver->wms[1]);
}


static bool
xserver_start(struct tw_xserver *xserver)
{
	int wm[2];
	struct wl_client *client = NULL;
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xserver->wl_display);
	const char *path = getenv("TW_XWAYLAND_PATH");
	if (!path)
		path = XSERVER_PATH; //default path, we may be able to

	if (!(xserver->sigusr1_source = wl_event_loop_add_signal(loop, SIGUSR1,
	                                                         handle_ready,
	                                                         xserver))) {
		tw_logl_level(TW_LOG_WARN, "Failed to watch signal SIGUSR1");
		return false;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm) < 0) {
		tw_logl_level(TW_LOG_WARN, "Xwm connects socketpair failed\n");
		return false;
	}
	xserver->wms[0] = wm[0];
	xserver->wms[1] = wm[1];

	client = tw_launch_client_complex(xserver->wl_display, path,
	                                  &xserver->process,
	                                  handle_xwayland_fork,
	                                  handle_xwayland_exec);
	if (client) {
		xserver->client = client;
		tw_reset_wl_list(&xserver->listeners.client_destroy.link);
		wl_client_add_destroy_listener(client,
		                               &xserver->listeners.client_destroy);
	}
	return client != NULL;
}

static int
handle_xwayland_socket_connected(int fd, uint32_t mask, void *data)
{
	struct tw_xserver *xserver = data;

	wl_event_source_remove(xserver->unix_source);
	wl_event_source_remove(xserver->abstract_source);
	xserver->unix_source = NULL;
	xserver->abstract_source = NULL;

	//TODO deal with server start failed
	xserver_start(xserver);
	return 0;
}

static bool
xserver_start_lazy(struct tw_xserver *xserver)
{
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xserver->wl_display);
	if (!(xserver->unix_source = wl_event_loop_add_fd(
		    loop, xserver->unix_fd, WL_EVENT_READABLE,
		    handle_xwayland_socket_connected, xserver)))
		return false;
	if (!(xserver->abstract_source = wl_event_loop_add_fd(
		    loop, xserver->abstract_fd, WL_EVENT_READABLE,
		    handle_xwayland_socket_connected, xserver))) {
		wl_event_source_remove(xserver->unix_source);
		xserver->unix_source = NULL;
		return false;
	}
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
		tw_logl_level(TW_LOG_WARN, "failed to bind to @%s: %s",
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

/*
 * We need to setup the sockets once we find a spot for the xdisplay, then we
 */
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

/*
 * A xserver has a lock "/tmp/.X<n>-lock" and listens to two sockets. One unix
 * socket "/tmp/.X11-unix/X<n>" and an abstract socket "@/tmp/.X11-unix/X<n>"
 * (prefix @ means abstract).
 *
 * The lock file contains only one line, the PID of the server process.
 */
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
unlink_display_sockets(struct tw_xserver *xserver)
{
	char path[64];

	snprintf(path, sizeof(path), SOCKET_FMT, xserver->display);
	unlink(path);

#ifdef __linux__
	snprintf(path, sizeof(path), SOCKET_FMT_ABS, 0, xserver->display);
	unlink(path);
#endif
	snprintf(path, sizeof(path), LOCK_FMT, xserver->display);
	unlink(path);
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
	unlink_display_sockets(xserver);

	xserver->abstract_fd = -1;
	xserver->unix_fd = -1;
	xserver->display = -1;
	xserver->name[0] = 0;
}

static void
notify_xserver_stop(struct wl_listener *listener, void *data)
{
	struct tw_xserver *xserver =
		wl_container_of(listener, xserver, listeners.client_destroy);

	wl_list_remove(&xserver->listeners.client_destroy.link);
	tw_reset_wl_list(&xserver->process.link);
	xserver->client = NULL;
	//TODO we should maybe restart xserver, but lets just purge the
	//resource first
	xserver_finish_process(xserver);
}

static void
notify_xserver_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xserver *server =
		wl_container_of(listener, server, listeners.display_destroy);
	tw_xserver_fini(server);
}

WL_EXPORT bool
tw_xserver_init(struct tw_xserver *server, struct wl_display *display,
                bool lazy)
{
	server->wl_display = display;
	server->abstract_fd = -1;
	server->unix_fd = -1;

	wl_signal_init(&server->signals.destroy);
	wl_signal_init(&server->signals.ready);
	wl_list_init(&server->listeners.display_destroy.link);
	wl_list_init(&server->listeners.client_destroy.link);

	if (!xserver_connect_display(server, display))
		return false;

	tw_set_display_destroy_listener(display,
	                                &server->listeners.display_destroy,
	                                notify_xserver_display_destroy);
	server->listeners.client_destroy.notify = notify_xserver_stop;

	if (lazy) {
		if (!xserver_start_lazy(server))
			goto err;
	} else {
		if (!xserver_start(server))
			goto err;
	}
	return true;

err:
	tw_xserver_fini(server);
	return false;
}

WL_EXPORT struct tw_xserver *
tw_xserver_create_global(struct wl_display *display, bool lazy)
{
	static struct tw_xserver s_server = {0};

	if (tw_xserver_init(&s_server, display, lazy))
		return &s_server;
	else
		return NULL;
}

WL_EXPORT void
tw_xserver_fini(struct tw_xserver *xserver)
{
	tw_reset_wl_list(&xserver->listeners.display_destroy.link);
	xserver_finish_process(xserver);
	xserver_finish_display(xserver);
}
