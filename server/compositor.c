/*
 * main.c - taiwins main functions
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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <linux/input.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wayland-server.h>
#include <libweston/libweston.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <os/file.h>
#include <shared_config.h>
#include "taiwins.h"
#include "compositor.h"

FILE *logfile;

static void
tw_compositor_get_socket(char *path)
{
	unsigned int socket_num = 0;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	while(true) {
		sprintf(path, "%s/wayland-%d", runtime_dir, socket_num);
		if (access(path, F_OK) != 0) {
			sprintf(path, "wayland-%d", socket_num);
			break;
		}
		socket_num++;
	}
}

static bool
tw_compositor_set_socket(struct wl_display *display, const char *name)
{
	if (name) {
		if (wl_display_add_socket(display, name)) {
			weston_log("failed to add socket %s", name);
			return false;
		}
	} else {
		name = wl_display_add_socket_auto(display);
		if (!name) {
			weston_log("failed to add socket %s", name);
			return false;
		}
	}
	return true;
}


static int
tw_compositor_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	weston_log("Caught signal %d\n", sig_num);
	wl_display_terminate(display);
	return 1;
}

static int
tw_compositor_sigchld(UNUSED_ARG(int sig_num), UNUSED_ARG(void *data))
{
	struct wl_list *head;
	struct tw_subprocess *subproc;
	int status;
	pid_t pid;

	head = tw_get_clients_head();

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		wl_list_for_each(subproc, head, link)
			if (pid == subproc->pid)
				break;

		if (&subproc->link == head) {
			weston_log("unknown process exited\n");
			continue;
		}

		wl_list_remove(&subproc->link);
		if (subproc->chld_handler)
			subproc->chld_handler(subproc, status);
	}
	if (pid < 0 && errno != ECHILD)
		weston_log("error in waiting child with status %s\n",
		           strerror(errno));
	return 1;
}

static void
tw_compositor_handle_exit(struct weston_compositor *c)
{
	wl_display_terminate(c->wl_display);
}


int main(int argc, char *argv[])
{

	struct wl_event_source *signals[4];
	const char *shellpath = (argc > 1) ? argv[1] : NULL;
	const char *launcherpath = (argc > 2) ? argv[2] : NULL;
	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);
	struct weston_log_context *context;
	struct weston_compositor *compositor;
	struct tw_config *config;
	char path[PATH_MAX];

	logfile = fopen("/tmp/taiwins_log", "w");
	weston_log_set_handler(tw_log, tw_log);

	tw_compositor_get_socket(path);
	if (!tw_compositor_set_socket(display, path))
		goto err_connect;

	//setup the signals
	signals[0] = wl_event_loop_add_signal(event_loop, SIGTERM,
	                                      tw_compositor_term_on_signal,
	                                      display);
	signals[1] = wl_event_loop_add_signal(event_loop, SIGINT,
	                                      tw_compositor_term_on_signal,
	                                      display);
	signals[2] = wl_event_loop_add_signal(event_loop, SIGQUIT,
	                                      tw_compositor_term_on_signal,
					      display);
	signals[3] = wl_event_loop_add_signal(event_loop, SIGCHLD,
	                                      tw_compositor_sigchld,
	                                      display);

	if (!signals[0] || !signals[1] || !signals[2] || !signals[3])
		goto err_signal;

	context = weston_log_ctx_compositor_create();
	//leak in here
	compositor = weston_compositor_create(display, context, NULL);
	weston_log_set_handler(tw_log, tw_log);
	compositor->exit = tw_compositor_handle_exit;

	tw_create_config_dir();
	tw_config_dir(path);
	strcat(path, "/config.lua");
	config = tw_config_create(compositor, tw_log);
	tw_config_register_object(config, "shell_path", (void *)shellpath);
	tw_config_register_object(config, "console_path", (void *)launcherpath);

	if (!tw_run_config(config) && tw_run_default_config(config))
		goto out;

	wl_display_run(display);
out:
	tw_config_destroy(config);
	weston_compositor_tear_down(compositor);
	weston_log_ctx_compositor_destroy(compositor);
	wl_display_destroy(display);
	weston_compositor_destroy(compositor);
	return 0;
err_signal:
	for (unsigned i = 0; i < 3; i++)
		wl_event_source_remove(signals[i]);
err_connect:
	wl_display_destroy(display);
	return -1;
}
