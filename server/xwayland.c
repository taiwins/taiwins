/*
 * xwayland.c - xwayland bindings
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

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <libweston/libweston.h>
#include <libweston/xwayland-api.h>
#include <wayland-util.h>

#include "helpers.h"
#include "taiwins.h"
#include "config.h"

#define XSERVER_PATH "Xwayland"

typedef OPTION(bool, enable) tw_xwayland_enable_t;

static struct tw_xwayland {
	struct weston_compositor *compositor;
	struct wl_listener compositor_destroy_listener;
	struct tw_config_component_listener config_component;
	struct tw_subprocess process;
	const struct weston_xwayland_api *api;

	/*>> runtime data */
	struct weston_xwayland *xwayland;
	struct wl_client *client;
	const char *display;
	pid_t pid;
	int wm[2], abstract_fd, unix_fd;
	struct wl_event_source *usr1_source;

	/**< options **/
	tw_xwayland_enable_t enabled;
} s_xwayland;


static void
tw_xwayland_handle_chld(struct tw_subprocess *chld, int status)
{
	struct tw_xwayland *xwayland =
		container_of(chld, struct tw_xwayland, process);

	struct weston_xwayland *server =
		xwayland->api->get(xwayland->compositor);

	xwayland->api->xserver_exited(server, status);
	wl_client_destroy(xwayland->client);
	xwayland->client = NULL;
	xwayland->display = NULL;
	xwayland->pid = -1;
	xwayland->abstract_fd = -1;
	xwayland->unix_fd = -1;
}


static void
tw_xwayland_on_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xwayland *xwayland =
		container_of(listener, struct tw_xwayland,
		             compositor_destroy_listener);

	if (xwayland->client && xwayland->pid > 0)
		kill(xwayland->pid, SIGTERM);
}

static int
tw_xwayland_handle_sigusr1(int signal_number, void *data)
{
	//TODO verify if the signal actually came from xserver
	struct tw_xwayland *xwayland = data;
	xwayland->api->xserver_loaded(xwayland->xwayland,
	                              xwayland->client, xwayland->wm[0]);
	wl_event_source_remove(xwayland->usr1_source);
	return 1;
}

static int
tw_xwayland_fork(pid_t pid, struct tw_subprocess *chld)
{
	struct tw_xwayland *xwayland =
		container_of(chld, struct tw_xwayland, process);

	if (pid == 0)
		signal(SIGUSR1, SIG_IGN);
	else {
                close(xwayland->wm[1]);
		xwayland->pid = pid;
	}
	return 0;
}

static int
tw_xwayland_exec(const char *path, struct tw_subprocess *chld)
{
	int fd;
	char abstract_fd_str[12], unix_fd_str[12], wm_fd_str[12];

	struct tw_xwayland *xwayland =
		container_of(chld, struct tw_xwayland, process);

	fd = dup(xwayland->abstract_fd);
	if (fd < 0)
		return -1;
	snprintf(abstract_fd_str, sizeof abstract_fd_str, "%d", fd);
	fd = dup(xwayland->unix_fd);
	if (fd < 0)
		return -1;
	snprintf(unix_fd_str, sizeof unix_fd_str, "%d", fd);
	fd = dup(xwayland->wm[1]);
	if (fd < 0)
		return -1;
	snprintf(wm_fd_str, sizeof wm_fd_str, "%d", fd);

	if (execlp(path, path, xwayland->display,
	           "-rootless",
	           "-listen", abstract_fd_str,
	           "-listen", unix_fd_str,
	           "-wm", wm_fd_str,
	           "-terminate", NULL) < 0) {
		tw_logl("exec of '%s %s -rootless "
		        "-listen %s -listen %s -wm %s "
		        "-terminate' failed: %s\n",
		        path, xwayland->display,
		        abstract_fd_str, unix_fd_str, wm_fd_str,
		        strerror(errno));
		return -1;
	}
	return 0;
}


static pid_t
tw_spawn_xwayland(void *user_data, const char *display, int abstract_fd,
                  int unix_fd)
{
	int wm[2];
	struct tw_xwayland *xwayland = user_data;
	const char *xserver = XSERVER_PATH;
	struct wl_client *client;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm) < 0) {
		weston_log("X wm connection socketpair failed\n");
		return 1;
	}
	xwayland->unix_fd = unix_fd;
	xwayland->abstract_fd = abstract_fd;
	xwayland->wm[0] = wm[0];
	xwayland->wm[1] = wm[1];
	xwayland->display = display;

	client = tw_launch_client_complex(xwayland->compositor, xserver,
	                                  &xwayland->process,
	                                  tw_xwayland_fork,
	                                  tw_xwayland_exec);
	xwayland->client = client;
	//ALSO, setup the sigchld handler

	return xwayland->pid;
}

static void
tw_xwayland_apply_config(struct tw_config *c, bool cleanup,
                         struct tw_config_component_listener *listener)
{
	struct weston_xwayland *xwayland;

	struct tw_xwayland *tw_xwayland =
		container_of(listener, struct tw_xwayland, config_component);
	if (cleanup)
		return;

	xwayland = tw_xwayland->api->get(tw_xwayland->compositor);
	tw_xwayland->xwayland = xwayland;
	if (tw_xwayland->enabled.enable && tw_xwayland->enabled.valid)
		tw_xwayland->api->listen(xwayland, tw_xwayland,
		                         tw_spawn_xwayland);
}

/*******************************************************************************
 * public functions
 ******************************************************************************/
struct tw_xwayland *
tw_xwayland_get_global()
{
	return &s_xwayland;
}

void
tw_xwayland_enable(struct tw_xwayland *xwayland, bool enable)
{
	xwayland->enabled.enable = enable;
	xwayland->enabled.valid = true;
}

bool
tw_setup_xwayland(struct weston_compositor *ec, struct tw_config *config)
{
	const struct weston_xwayland_api *api;
	struct wl_event_loop *loop;

	int (*xwayland_init)(struct weston_compositor *ec);

	if (!(xwayland_init =
	      tw_load_weston_module("xwayland.so", "weston_module_init")))
		return false;
	if (xwayland_init(ec) < 0)
		return false;

	api = weston_xwayland_get_api(ec);
	if (!api) {
		weston_log("faild to load xwayland API.\n");
		return false;
	}

	s_xwayland.compositor = ec;
	s_xwayland.api = api;
	s_xwayland.compositor_destroy_listener.notify = tw_xwayland_on_destroy;
	s_xwayland.process.chld_handler = tw_xwayland_handle_chld;
	s_xwayland.client = NULL;
	s_xwayland.pid = -1;
	s_xwayland.wm[0] = -1;
	s_xwayland.wm[1] = -1;
	//defaulty to true
	s_xwayland.enabled.enable = true;
	s_xwayland.enabled.valid = true;

	wl_list_init(&s_xwayland.compositor_destroy_listener.link);
	wl_signal_add(&ec->destroy_signal,
	              &s_xwayland.compositor_destroy_listener);

	wl_list_init(&s_xwayland.config_component.link);
	s_xwayland.config_component.apply = tw_xwayland_apply_config;
	tw_config_add_component(config, &s_xwayland.config_component);

	loop = wl_display_get_event_loop(ec->wl_display);
	s_xwayland.usr1_source =
		wl_event_loop_add_signal(loop, SIGUSR1,
		                         tw_xwayland_handle_sigusr1,
		                         &s_xwayland);

	return true;
}
