/*
 * main.c - taiwins start point
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

#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/wait.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/profiler.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/seat.h>

#include <ctypes/helpers.h>
#include "bindings.h"
#include "renderer/renderer.h"
#include "backend.h"
#include "input.h"
#include "config.h"
#include "taiwins/objects/utils.h"

struct tw_server {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */

	/* globals */
	struct tw_backend *backend;
	struct tw_bindings *bindings;
	struct tw_config *config;

	/* seats */
	struct tw_seat_events seat_events[8];
	struct wl_listener seat_add;
	struct wl_listener seat_remove;
};

struct tw_options {
	const char *test_case;
	struct tw_subprocess test_client;
};

static bool
bind_backend(struct tw_server *server)
{
	//handle backend
	server->backend = tw_backend_create_global(server->display,
	                                           tw_layer_renderer_create);
	if (!server->backend) {
		tw_logl("EE: failed to create backend\n");
		return false;
	}
	tw_backend_defer_outputs(server->backend, true);

	return true;
}

static bool
bind_config(struct tw_server *server)
{
	server->bindings = tw_bindings_create(server->display);
	if (!server->bindings)
		goto err_binding;
	server->config = tw_config_create(server->backend, server->bindings);
	if (!server->config)
		goto err_config;
	return true;
err_config:
	tw_bindings_destroy(server->bindings);
err_binding:
	return false;
}

static void
notify_adding_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_add);
	struct tw_backend_seat *seat = data;
	uint32_t i = seat->idx;
	tw_seat_events_init(&server->seat_events[i], seat,
	                    server->bindings);
}

static void
notify_removing_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_remove);
	struct tw_backend_seat *seat = data;
	uint32_t i = seat->idx;
	tw_seat_events_fini(&server->seat_events[i]);
}

static void
bind_listeners(struct tw_server *server)
{
	tw_signal_setup_listener(&server->backend->seat_add_signal,
	                         &server->seat_add,
	                         notify_adding_seat);
	tw_signal_setup_listener(&server->backend->seat_rm_signal,
	                         &server->seat_remove,
	                         notify_removing_seat);
}

static bool
tw_server_init(struct tw_server *server, struct wl_display *display)
{
	server->display = display;
	server->loop = wl_display_get_event_loop(display);
	if (!bind_backend(server))
		return false;
	if (!bind_config(server))
		return false;
	bind_listeners(server);
	return true;
}

static void
tw_server_fini(struct tw_server *server)
{
	tw_config_destroy(server->config);
	tw_bindings_destroy(server->bindings);
}


static void
print_option(void)
{}

static bool
drop_permissions(void) {
	if (getuid() != geteuid() || getgid() != getegid()) {
		// Set the gid and uid in the correct order.
		if (setgid(getgid()) != 0) {
			return false;
		}
		if (setuid(getuid()) != 0) {
			return false;
		}
	}
	if (setgid(0) != -1 || setuid(0) != -1) {
		return false;
	}
	return true;
}

static bool
parse_options(struct tw_options *options, int argc, char **argv)
{
	bool ret = true;
	const char *arg;
	for (int cursor = 1, advance = 1; cursor < argc; cursor+=advance) {
		advance = 1;
		arg = argv[cursor];
		if (!strcmp(arg, "--help")) {
			print_option();
			return false;
		} else if (!strcmp(arg, "-t") || !strcmp(arg, "--test")) {
			if (cursor+1 < argc) {
				options->test_case = argv[cursor+1];
				advance = 2;
				continue;
			} else {
				ret = false;
				break;
			}
		}
	}
	return ret;
}

static bool
tw_set_socket(struct wl_display *display)
{
	char path[PATH_MAX];
	unsigned int socket_num = 0;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	//get socket
	while(true) {
		sprintf(path, "%s/wayland-%d", runtime_dir, socket_num);
		if (access(path, F_OK) != 0) {
			sprintf(path, "wayland-%d", socket_num);
			break;
		}
		socket_num++;
	}
	if (wl_display_add_socket(display, path)) {
		tw_logl("EE:failed to add socket %s", path);
		return false;
	}
	return true;
}

static int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %s\n", strsignal(sig_num));
	wl_display_terminate(display);
	return 1;
}

static int
tw_handle_sigchld(int sig_num, void *data)
{
	struct wl_list *head;
	struct tw_subprocess *subproc;
	int status;
	pid_t pid;

	head = tw_get_clients_head();
	tw_logl("Caught signal %s\n", strsignal(sig_num));

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		wl_list_for_each(subproc, head, link)
			if (pid == subproc->pid)
				break;

		if (&subproc->link == head) {
			tw_logl("unknown process exited\n");
			continue;
		}

		wl_list_remove(&subproc->link);
		if (subproc->chld_handler)
			subproc->chld_handler(subproc, status);
	}
	if (pid < 0 && errno != ECHILD)
		tw_logl("error in waiting child with status %s\n",
		           strerror(errno));
	return 1;
}

int
main(int argc, char *argv[])
{
	int ret = 0;
	struct tw_server ec = {0};
	struct wl_event_source *signals[4];
	struct wl_display *display;
	struct wl_event_loop *loop;
	struct tw_options options = {0};

	if (!parse_options(&options, argc, argv))
		return -1;
	tw_logger_use_file(stderr);

	display = wl_display_create();
	if (!display) {
		ret = -1;
		tw_logl("EE: failed to create wayland display\n");
		goto err_create_display;
	}
	loop = wl_display_get_event_loop(display);
	if (!loop) {
		ret = -1;
		tw_logl("EE: failed to get event_loop from display\n");
		goto err_event_loop;
	}
	if (!tw_set_socket(display)) {
		ret = -1;
		goto err_socket;
	}
	if (!tw_profiler_open(display, "/tmp/taiwins-profiler.json"))
		goto err_profiler;

	signals[0] = wl_event_loop_add_signal(loop, SIGTERM,
	                                      tw_term_on_signal, display);
	signals[1] = wl_event_loop_add_signal(loop, SIGINT,
	                                      tw_term_on_signal, display);
	signals[2] = wl_event_loop_add_signal(loop, SIGQUIT,
	                                      tw_term_on_signal, display);
	signals[3] = wl_event_loop_add_signal(loop, SIGCHLD,
	                                      tw_handle_sigchld, display);
	if (!signals[0] || !signals[1] || !signals[2] || !signals[3])
		goto err_signal;
	if (!tw_server_init(&ec, display))
		goto err_backend;
	if (!drop_permissions())
		goto err_permission;

	if (options.test_case)
		tw_launch_client(display, options.test_case,
		                 &options.test_client);

	if (!tw_run_config(ec.config)) {
		if (!tw_run_default_config(ec.config))
			goto err_config;
	}
	//run the loop
	tw_backend_flush(ec.backend);
	wl_display_run(ec.display);

	//end.
err_permission:
err_config:
        tw_server_fini(&ec);
err_backend:
err_signal:
	for (int i = 0; i < 4; i++)
		wl_event_source_remove(signals[i]);
err_event_loop:
	tw_profiler_close();
err_profiler:
err_socket:
	wl_display_destroy(ec.display);
err_create_display:
	tw_logger_close();
	return ret;
}
