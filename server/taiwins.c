/*
 * taiwins.c - taiwins server shared functions
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include "wlr/types/wlr_matrix.h"
#include "ctypes/helpers.h"
#include "desktop/xdg.h"
#include <wayland-server-core.h>
#include <wayland-util.h>
#endif

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>
#include <wayland-server.h>
#include <wlr/types/wlr_matrix.h>
#include <ctypes/os/os-compatibility.h>

#include <renderer/renderer.h>
#include <backend/backend.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include "desktop/shell.h"
#include "taiwins.h"

void *
tw_load_weston_module(const char *name, const char *entrypoint)
{
	void *module, *init;

	if (name == NULL || entrypoint == NULL)
		return NULL;

	//our modules are in the rpath as we do not have the
	//LIBWESTON_MODULEDIR, so we need to test name and
	module = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
	if (module) {
		tw_logl("Module '%s' already loaded\n", name);
		return NULL;
	} else {
		module = dlopen(name, RTLD_NOW);
		if (!module) {
			tw_logl("Failed to load the module %s\n", name);
			return NULL;
		}
	}

	init = dlsym(module, entrypoint);
	if (!init) {
		tw_logl("Faild to lookup function in module: %s\n",
		           dlerror());
		dlclose(module);
		return NULL;
	}
	return init;

}


/******************************************************************************
 * tw_server, this is probably a bad place to set it up
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
notify_surface_created(struct wl_listener *listener, void *data)
{
}

static void
bind_listeners(struct tw_server *server)
{
	//seat add
	wl_list_init(&server->seat_add.link);
	server->seat_add.notify = notify_adding_seat;
	wl_signal_add(&server->backend->seat_add_signal,
	              &server->seat_add);
	//seat remove
	wl_list_init(&server->seat_remove.link);
	server->seat_remove.notify = notify_removing_seat;
	wl_signal_add(&server->backend->seat_rm_signal,
	              &server->seat_remove);

	//surface created
	wl_list_init(&server->surface_created.link);
	server->surface_created.notify = notify_surface_created;
	wl_signal_add(&server->backend->surface_manager.surface_created_signal,
	              &server->surface_created);
}

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

	server->wlr_backend = tw_backend_get_backend(server->backend);
	server->wlr_renderer = wlr_backend_get_renderer(server->wlr_backend);
	return true;
}

static void
bind_globals(struct tw_server *server)
{
	//bindings
	server->binding_state =
		tw_bindings_create(server->display);
	tw_bindings_add_dummy(server->binding_state);

        server->tw_shell =
		tw_shell_create_global(server->display, server->backend, NULL);
	assert(server->tw_shell);

	server->tw_xdg =
		tw_xdg_create_global(server->display, server->tw_shell,
		                     server->backend);
	assert(server->tw_xdg);
}

bool
tw_server_init(struct tw_server *server, struct wl_display *display)
{
	server->display = display;
	server->loop = wl_display_get_event_loop(display);
	if (!bind_backend(server))
		return false;
	bind_globals(server);
	bind_listeners(server);

	return true;
}
