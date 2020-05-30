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
#include <unistd.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>

// TODO remove libweston related stuff.
#include "taiwins.h"
#include "backend/backend.h"
// For this, we would probably start come up with a backend code

struct tw_compositor {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */
	struct tw_backend *backend;

        //wlr datas
	struct wlr_backend *wlr_backend;
	struct wlr_renderer *wlr_renderer;
	struct wlr_compositor *wlr_compositor;
	struct wlr_data_device_manager *wlr_data_device;

	//binding
	struct wl_listener seat_add_listener;
	struct wl_listener seat_change_listener;
	struct {
		//keyboard
		struct wl_listener key_listener;
		struct wl_listener mod_listener;
		//pointer
		struct wl_listener btn_listener;
		//touch
		struct wl_listener tch_listener;
	} binding_events;
};


int
main(int argc, char *argv[])
{
	int ret = 0;
	struct tw_compositor ec = {0};
	struct wl_event_source *signals[4];

	tw_logfile = fopen("/tmp/taiwins-log", "w");

	ec.display = wl_display_create();
	if (!ec.display) {
		ret = -1;
		tw_logl("EE: failed to create wayland display\n");
		goto err_create_display;
	}
	ec.loop = wl_display_get_event_loop(ec.display);
	if (!ec.loop) {
		ret = -1;
		tw_logl("EE: failed to get wayland event loop\n");
		goto err_get_loop;
	}

	if (!tw_set_socket(ec.display)) {
		ret = -1;
		goto err_socket;
	}
	signals[0] = wl_event_loop_add_signal(ec.loop, SIGTERM,
	                                      tw_term_on_signal, ec.display);
	signals[1] = wl_event_loop_add_signal(ec.loop, SIGINT,
	                                      tw_term_on_signal, ec.display);
	signals[2] = wl_event_loop_add_signal(ec.loop, SIGQUIT,
	                                      tw_term_on_signal, ec.display);
	signals[3] = wl_event_loop_add_signal(ec.loop, SIGCHLD,
	                                      tw_handle_sigchld, ec.display);
	if (!signals[0] || !signals[1] || !signals[2] || !signals[3])
		goto err_signal;


	//handle backend
	ec.backend = tw_backend_create_global(ec.display);
	if (!ec.backend) {
		ret = -1;
		tw_logl("EE: failed to create backend\n");
		goto err_create_backend;
	}
	tw_backend_defer_outputs(ec.backend, true);

	ec.wlr_backend = tw_backend_get_backend(ec.backend);
	ec.wlr_renderer = wlr_backend_get_renderer(ec.wlr_backend);

	//TODO we do not have input devices
	//build on top of the binding system

	//create various wl globals
	ec.wlr_compositor =
		wlr_compositor_create(ec.display, ec.wlr_renderer);
	ec.wlr_data_device =
		wlr_data_device_manager_create(ec.display);

	//run the loop
	tw_backend_flush(ec.backend);
	wl_display_run(ec.display);

err_signal:
	for (int i = 0; i < 4; i++)
		wl_event_source_remove(signals[i]);
err_create_backend:
err_get_loop:
err_socket:
	wl_display_destroy(ec.display);
err_create_display:
	fclose(tw_logfile);
	return ret;
}
