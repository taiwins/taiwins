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

#include "binding/bindings.h"
#include "ctypes/helpers.h"
#include "seat/seat.h"
#include "taiwins.h"
#include "bindings.h"
#include "backend/backend.h"
#include "input.h"


struct tw_server {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */
	struct tw_backend *backend;

        //wlr datas
	struct wlr_backend *wlr_backend;
	struct wlr_renderer *wlr_renderer;
	struct wlr_compositor *wlr_compositor;
	struct wlr_data_device_manager *wlr_data_device;
	struct tw_bindings *binding_state;

	//seats
	struct tw_seat_events seat_events[8];
	struct wl_listener seat_add;
	struct wl_listener seat_remove;

};

/******************************************************************************
 * setups
 *****************************************************************************/

static void
notify_adding_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_add);
	struct tw_backend_seat *seat = data;
	uint32_t i = seat->idx;
	tw_seat_events_init(&server->seat_events[i], seat,
	                    server->binding_state);
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
	wl_list_init(&server->seat_add.link);
	server->seat_add.notify = notify_adding_seat;
	wl_signal_add(&server->backend->seat_add_signal,
	              &server->seat_add);
	wl_list_init(&server->seat_remove.link);
	server->seat_remove.notify = notify_removing_seat;
	wl_signal_add(&server->backend->seat_rm_signal,
	              &server->seat_remove);
}

static bool
bind_backend(struct tw_server *server)
{
	//handle backend
	server->backend = tw_backend_create_global(server->display);
	if (!server->backend) {
		tw_logl("EE: failed to create backend\n");
		return false;
	}
	tw_backend_defer_outputs(server->backend, true);

	server->wlr_backend = tw_backend_get_backend(server->backend);
	server->wlr_renderer = wlr_backend_get_renderer(server->wlr_backend);
	return true;
}

static void
bind_globals(struct tw_server *server)
{
	//declare various globals
	server->wlr_compositor =
		wlr_compositor_create(server->display,
		                      server->wlr_renderer);
	server->wlr_data_device =
		wlr_data_device_manager_create(server->display);

	wl_display_init_shm(server->display);

	server->binding_state =
		tw_bindings_create(server->display);
	tw_bindings_add_dummy(server->binding_state);
}

int
main(int argc, char *argv[])
{
	int ret = 0;
	struct tw_server ec = {0};
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
	if (!bind_backend(&ec))
		goto err_backend;
	bind_globals(&ec);
	bind_listeners(&ec);

	//run the loop
	tw_backend_flush(ec.backend);
	wl_display_run(ec.display);

err_backend:
err_signal:
	for (int i = 0; i < 4; i++)
		wl_event_source_remove(signals[i]);
err_get_loop:
err_socket:
	wl_display_destroy(ec.display);
err_create_display:
	fclose(tw_logfile);
	return ret;
}
