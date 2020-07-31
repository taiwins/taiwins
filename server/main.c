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
#include "binding/bindings.h"
#include "taiwins.h"
#include "bindings.h"
#include "backend.h"
#include "input.h"

struct tw_options {
	const char *test_case;
	struct tw_subprocess test_client;
};

static void
print_option(void)
{

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

	if (options.test_case)
		tw_launch_client(display, options.test_case,
		                 &options.test_client);

	//run the loop
	tw_backend_flush(ec.backend);
	wl_display_run(ec.display);

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
