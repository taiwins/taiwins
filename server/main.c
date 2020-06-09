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

#include <ctypes/helpers.h>
#include "binding/bindings.h"
#include "seat/seat.h"
#include "taiwins.h"
#include "bindings.h"
#include "backend/backend.h"
#include "input.h"




int
main(int argc, char *argv[])
{
	int ret = 0;
	struct tw_server ec = {0};
	struct wl_event_source *signals[4];
	struct wl_display *display;
	struct wl_event_loop *loop;

	tw_logger_open("/tmp/taiwins-log");

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
	//run the loop
	tw_backend_flush(ec.backend);
	wl_display_run(ec.display);

err_backend:
err_signal:
	for (int i = 0; i < 4; i++)
		wl_event_source_remove(signals[i]);
err_event_loop:
err_socket:
	wl_display_destroy(ec.display);
err_create_display:
	tw_logger_close();
	return ret;
}
