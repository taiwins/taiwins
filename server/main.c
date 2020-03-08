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

#include <libweston/libweston.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "backend.h"
#include "desktop.h"
#include "os/file.h"
#include "taiwins.h"
#include "config.h"
#include "bindings.h"

//remove this two later

struct tw_compositor {
	struct weston_compositor *ec;

	struct taiwins_apply_bindings_listener add_binding;
	struct taiwins_config_component_listener config_component;
};


static FILE *logfile = NULL;

static int
tw_log(const char *format, va_list args)
{
	return vfprintf(logfile, format, args);
}

static void
tw_compositor_quit(struct weston_keyboard *keyboard,
	     const struct timespec *time,
	     uint32_t key, uint32_t option,
	     void *data)
{
	struct weston_compositor *compositor = data;
	fprintf(stderr, "quitting taiwins\n");
	struct wl_display *wl_display =
		compositor->wl_display;
	wl_display_terminate(wl_display);
}

static bool
tw_compositor_add_bindings(struct tw_bindings *bindings, struct taiwins_config *c,
			struct taiwins_apply_bindings_listener *listener)
{
	struct tw_compositor *tc = container_of(listener, struct tw_compositor, add_binding);
	const struct tw_key_press *quit_press =
		taiwins_config_get_builtin_binding(c, TW_QUIT_BINDING)->keypress;
	tw_bindings_add_key(bindings, quit_press, tw_compositor_quit, 0, tc->ec);
	return true;
}

static void
tw_compositor_init(struct tw_compositor *tc, struct weston_compositor *ec,
		   struct taiwins_config *config)
{
	tc->ec = ec;
	struct xkb_rule_names sample_rules =  {
			.rules = NULL,
			.model = strdup("pc105"),
			.layout = strdup("us"),
			.options = strdup("ctrl:swap_lalt_lctl"),
		};
	weston_compositor_set_xkb_rule_names(ec, &sample_rules);

	wl_list_init(&tc->add_binding.link);
	tc->add_binding.apply = tw_compositor_add_bindings;
	taiwins_config_add_apply_bindings(config, &tc->add_binding);
}

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
        /* setenv("WAYLAND_DISPLAY", name, 1); */
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

int main(int argc, char *argv[], char *envp[])
{
	int error = 0;
	struct tw_compositor tc;
	struct wl_event_source *signals[4];
	const char *shellpath = (argc > 1) ? argv[1] : NULL;
	const char *launcherpath = (argc > 2) ? argv[2] : NULL;
	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);
	struct weston_log_context *context;
	struct weston_compositor *compositor;
	char path[100];

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
	//TODO handle chld exit
	if (!signals[0] || !signals[1] || !signals[2])
		goto err_signal;

	context = weston_log_ctx_compositor_create();
	compositor = weston_compositor_create(display, context, NULL);

	weston_log_set_handler(tw_log, tw_log);

	char *xdg_dir = getenv("XDG_CONFIG_HOME");
	if (!xdg_dir)
		xdg_dir = getenv("HOME");
	strcpy(path, xdg_dir);
	strcat(path, "/config.lua");
	struct taiwins_config *config =
		taiwins_config_create(compositor, tw_log);

	tw_compositor_init(&tc, compositor, config);
	tw_setup_backend(compositor, config);
	weston_compositor_wake(compositor);
	struct shell *sh = announce_shell(compositor, shellpath, config);
	announce_console(compositor, sh, launcherpath, config);
	announce_desktop(compositor, sh, config);

	error = !taiwins_run_config(config, path);
	if (error) {
		goto out;
	}
	compositor->kb_repeat_delay = 400;
	compositor->kb_repeat_rate = 40;

	wl_display_run(display);

out:
	taiwins_config_destroy(config);
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
