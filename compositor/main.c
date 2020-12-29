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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <sys/wait.h>
#include <limits.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/profiler.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/egl.h>
#include <ctypes/helpers.h>
#include <ctypes/os/file.h>

#include <taiwins/backend.h>
#include <taiwins/shell.h>
#include <taiwins/xdg.h>
#include <taiwins/engine.h>
#include <taiwins/input_device.h>
#include <taiwins/render_context.h>
#include <taiwins/render_pipeline.h>

#include "input.h"
#include "bindings.h"
#include "config.h"


struct tw_options {
	const char *test_case;
	const char *shell_path;
	const char *console_path;
	const char *log_path;
	const char *profiling_path;
};

struct tw_server {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */

	/* globals */
	struct tw_backend *backend;
	struct tw_engine *engine;
	struct tw_bindings *bindings;
	struct tw_render_context *ctx;
	struct tw_config *config;

	/* seats */
	struct tw_seat_events seat_events[8];
	struct wl_listener seat_add;
	struct wl_listener seat_remove;
};

static bool
bind_backend(struct tw_server *server)
{
	//handle backend
	server->backend = tw_backend_create_auto(server->display);

	if (!server->backend) {
		tw_logl("EE: failed to create backend\n");
		return false;
	}

	server->engine = tw_engine_create_global(server->display,
	                                         server->backend);
	if (!server->engine) {
		tw_logl_level(TW_LOG_ERRO, "failed to create output");
		return false;
	}

	return true;
}

static bool
bind_config(struct tw_server *server)
{
	server->bindings = tw_bindings_create(server->display);
	if (!server->bindings)
		goto err_binding;
	server->config = tw_config_create(server->engine, server->bindings,
	                                  TW_CONFIG_TYPE_LUA);
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
		wl_container_of(listener, server, seat_add);
	struct tw_engine_seat *seat = data;

	tw_seat_events_init(&server->seat_events[seat->idx],
	                          seat, server->bindings);
}

static void
notify_removing_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_remove);
	struct tw_engine_seat *seat = data;

	tw_seat_events_fini(&server->seat_events[seat->idx]);
}

static void
bind_listeners(struct tw_server *server)
{
	tw_signal_setup_listener(&server->engine->signals.seat_created,
	                         &server->seat_add,
	                         notify_adding_seat);
	tw_signal_setup_listener(&server->engine->signals.seat_remove,
	                         &server->seat_remove,
	                         notify_removing_seat);
}

static bool
bind_render(struct tw_server *server)
{
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(server->backend);

	server->ctx = tw_render_context_create_egl(server->display, opts);

	if (!server->ctx)
		return false;

	struct tw_render_pipeline *pipeline =
		tw_egl_render_pipeline_create_default(
			server->ctx, &server->engine->layers_manager);
	if (!pipeline)
		return false;

	wl_list_insert(server->ctx->pipelines.prev, &pipeline->link);
	return true;
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
	if (!bind_render(server))
		return false;

	bind_listeners(server);
	return true;
}

static void
tw_server_fini(struct tw_server *server)
{
	/* tw_config_destroy(server->config); */
	tw_bindings_destroy(server->bindings);
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

static bool
drop_permissions(void)
{
	if (getuid() != geteuid() || getgid() != getegid()) {
		// Set the gid and uid in the correct order.
		if (setgid(getgid()) != 0)
			return false;
		if (setuid(getuid()) != 0)
			return false;
	}
	if (setgid(0) != -1 || setuid(0) != -1) {
		return false;
	}
	return true;
}

static void
print_help(void)
{
	const char* usage =
		"Usage: taiwins [options] [command]\n"
		"\n"
		"  -h, --help             Show help message and quit.\n"
		"  -v, --version          Show the version number and quit.\n"
		"  -s, --shell            Specify the taiwins shell client path.\n"
		"  -c, --console          Specify the taiwins console client path.\n"
		"  -l, --log-path         Specify the logging path.\n"
		"  -n, --no-shell         Launch taiwins without shell client.\n"
		"  -p, --profiling-path   Specify the profiling path.\n"
		"\n";
	fprintf(stdout, "%s", usage);
}

static void
verify_set_executable(const char **dst, const char *src)
{
	char full_path[PATH_MAX];

	if (find_executable(src, full_path, PATH_MAX)) {
		*dst = src;
	} else {
		tw_logl_level(TW_LOG_ERRO, "executable %s not found", src);
		exit(EXIT_FAILURE);
	}
}

static void
parse_options(struct tw_options *options, int argc, char **argv)
{
	int c;
	bool no_shell = false;
	char full_path[PATH_MAX];

	static const struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'v'},
		{"shell", required_argument, NULL, 's'},
		{"console", required_argument, NULL, 'c'},
		{"log-path", required_argument, NULL, 'l'},
		{"no-shell", no_argument, NULL, 'n'},
		{"profiling-path", required_argument, NULL, 'p'},
		{0,0,0,0},
	};
	//init options
	memset(options, 0, sizeof(*options));
	options->shell_path =
		find_executable("taiwins-shell", full_path, PATH_MAX) ?
		"taiwins-shell" : NULL;
	options->console_path =
		find_executable("taiwins-console", full_path, PATH_MAX) ?
		"taiwins-console" : NULL;

	while (1) {
		int opt_index = 0;
		c = getopt_long(argc, argv, "hvns:c:l:p:",
		                long_options, &opt_index);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			print_help();
			exit(EXIT_SUCCESS);
			break;
		case 'v':
			fprintf(stdout, "Taiwins version " _TW_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;
		case 'n':
			no_shell = true;
			break;
		case 's':
			verify_set_executable(&options->shell_path, optarg);
			break;
		case 'c':
			verify_set_executable(&options->console_path, optarg);
			break;
		case 'l':
			options->log_path = optarg;
			break;
		case 'p':
			options->profiling_path = optarg;
			break;
		default:
			fprintf(stderr, "uknown argument %c\n.", c);
			exit(EXIT_FAILURE);
			break;
		}
	}
	if (no_shell) {
		options->shell_path = NULL;
		options->console_path = NULL;
	}
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

	parse_options(&options, argc, argv);
	if (!options.log_path)
		tw_logger_use_file(stderr);
	else
		tw_logger_open(options.log_path);

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
	if (!options.profiling_path)
		options.profiling_path = "/dev/null";
	if (!tw_profiler_open(display, options.profiling_path))
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

	tw_config_register_object(ec.config, TW_CONFIG_SHELL_PATH,
	                          (void *)options.shell_path);
	tw_config_register_object(ec.config, TW_CONFIG_CONSOLE_PATH,
	                          (void *)options.console_path);
	if (!tw_run_config(ec.config)) {
		if (!tw_run_default_config(ec.config))
			goto err_config;
	}
	//run the loop
	tw_backend_start(ec.backend, ec.ctx);
	wl_display_run(ec.display);

	//end.
err_config:
err_permission:
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
