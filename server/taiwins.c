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
#include <libweston/libweston.h>
#include <wlr/types/wlr_matrix.h>
#include <ctypes/os/os-compatibility.h>

#include <renderer/renderer.h>
#include <backend/backend.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include "taiwins.h"

void
tw_lose_surface_focus(struct weston_surface *surface)
{
	struct weston_compositor *ec = surface->compositor;
	struct weston_seat *seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(seat, &ec->seat_list, link) {
		 keyboard = weston_seat_get_keyboard(seat);
		if (keyboard &&
		    (weston_surface_get_main_surface(keyboard->focus) == surface))
			weston_keyboard_set_focus(keyboard, NULL);
		//it maynot be a good idea to do the pointer and touch as well,
		//since FIRST only keyboard gets the focus of a surface, the
		//rest gets the focus from view; SECOND if we do this when we
		//need focused output, there is no thing we can do
	}
}

void
tw_focus_surface(struct weston_surface *surface)
{
	struct weston_seat *active_seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(active_seat, &surface->compositor->seat_list, link) {
		keyboard = active_seat->keyboard_state;
		if (keyboard) {
			weston_keyboard_set_focus(keyboard, surface);
			break;
		}
	}
}

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_touch *touch = weston_seat_get_touch(seat);
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		/* Priority has touch focus, then pointer and
		 * then keyboard focus. We should probably have
		 * three for loops and check frist for touch,
		 * then for pointer, etc. but unless somebody has some
		 * objections, I think this is sufficient. */
		if (touch && touch->focus)
			output = touch->focus->output;
		else if (pointer && pointer->focus)
			output = pointer->focus->output;
		else if (keyboard && keyboard->focus)
			output = keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

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
render_surface_texture(struct tw_surface *surface,
                       struct wlr_renderer *renderer,
                       struct wlr_output *wlr_output)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	//TODO, wlr_matrix is row major and the it uses a total different
	//coordinate system, sadly I cannot take advantage of it.

	float transform[9];
	struct wlr_texture *texture = surface->buffer.handle.ptr;
	if (texture) {
		wlr_matrix_transpose(transform,
		                     surface->geometry.transform.d);
		wlr_matrix_multiply(transform,
		                    wlr_output->transform_matrix,
		                    transform);
		/* wlr_render_texture_with_matrix(renderer, texture, */
		/*                                transform, 1.0); */
		wlr_render_texture(renderer, texture,
		                   wlr_output->transform_matrix,
		                   surface->geometry.xywh.x,
		                   surface->geometry.xywh.y,
		                   1.0f);
	}
	tw_surface_flush_frame(surface,
	                       now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

void
new_output_frame(struct wl_listener *listener, void *data)
{
	int width, height;
	struct tw_server *server =
		container_of(listener, struct tw_server, output_frame);
	struct tw_backend_output *output = data;
	struct wlr_renderer *renderer = server->wlr_renderer;
	struct wlr_output *wlr_output = output->wlr_output;
	struct tw_surface *surface;

	tw_server_build_surface_list(server);
	/* tw_server_stack_damage(server); */

	if (!wlr_output_attach_render(output->wlr_output, NULL))
		return;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	wlr_renderer_begin(renderer, width, height);

        float color[4] = {0.3, 0.3, 0.3, 1.0};
	wlr_renderer_clear(renderer, color);

	wl_list_for_each(surface, &server->backend->layers_manager.views,
	                 links[TW_VIEW_GLOBAL_LINK])
		render_surface_texture(surface, renderer, wlr_output);

	wlr_renderer_end(renderer);
	wlr_output_commit(output->wlr_output);
}


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
	struct tw_surface *surface = data;
	struct tw_server *server =
		container_of(listener, struct tw_server, surface_created);

	wl_list_insert(server->backend->layers_manager.cursor_layer.views.prev,
               &surface->links[TW_VIEW_LAYER_LINK]);
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

	//the frame callback, here we could have a choice in the future, if
	//renderer offers different frame type.
	/* wl_list_init(&server->output_frame.link); */
	/* server->output_frame.notify = notify_new_output_frame; */
	/* wl_signal_add(&server->backend->output_frame_signal, */
	/*               &server->output_frame); */
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
