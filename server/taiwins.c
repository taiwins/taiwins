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
#include "ctypes/helpers.h"
#include "objects/compositor.h"
#include "objects/dmabuf.h"
#include <wayland-server-core.h>
#endif

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>
#include <wayland-server.h>
#include <libweston/libweston.h>
#include <wayland-util.h>
#include <ctypes/os/os-compatibility.h>

#include <backend/render/renderer.h>
#include <backend/backend.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include "taiwins.h"

bool
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

int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %s\n", strsignal(sig_num));
	wl_display_terminate(display);
	return 1;
}

int
tw_handle_sigchld(int sig_num, UNUSED_ARG(void *data))
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
notify_create_wl_surface(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server,
		             surface_create_listener);
	struct tw_event_new_wl_surface *event = data;
	//TODO, maybe additional callbacks
	tw_surface_create(event->client, event->version, event->id,
	                  &server->surface_manager);
}

static void
notify_create_wl_subsurface(struct wl_listener *listener, void *data)
{
	struct tw_event_get_wl_subsurface *event = data;
	struct tw_surface *surface, *parent;
	surface = tw_surface_from_resource(event->surface);
	parent = tw_surface_from_resource(event->parent_surface);

	tw_subsurface_create(event->client, event->version, event->id,
	                     surface, parent);
}

static void
notify_create_wl_region(struct wl_listener *listener, void *data)
{
	struct tw_event_new_wl_region *event = data;
	tw_region_create(event->client, event->version, event->id);
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

	wl_list_init(&server->surface_create_listener.link);
	server->surface_create_listener.notify = notify_create_wl_surface;
	wl_signal_add(&server->compositor->surface_create,
	              &server->surface_create_listener);

	wl_list_init(&server->subsurface_create_listener.link);
	server->subsurface_create_listener.notify =
		notify_create_wl_subsurface;
	wl_signal_add(&server->compositor->subsurface_get,
	              &server->subsurface_create_listener);

	wl_list_init(&server->region_create_listener.link);
	server->region_create_listener.notify =
		notify_create_wl_region;
	wl_signal_add(&server->compositor->region_create,
	              &server->region_create_listener);
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
	server->compositor =
		tw_compositor_create_global(server->display);

	server->wlr_data_device =
		wlr_data_device_manager_create(server->display);

	wl_display_init_shm(server->display);

	server->dma_engine = tw_dmabuf_create_global(server->display);

	tw_surface_manager_init(&server->surface_manager);

	if (server->wlr_renderer) {
		server->surface_manager.buffer_import.buffer_import =
			tw_renderer_import_buffer;
		server->surface_manager.buffer_import.callback =
			server->wlr_renderer;
		//dma engine
		server->dma_engine->import_buffer.import_buffer =
			tw_renderer_test_import_dmabuf;
		server->dma_engine->import_buffer.callback =
			server->wlr_renderer;
		server->dma_engine->format_request.format_request =
			tw_renderer_format_request;
		server->dma_engine->format_request.modifiers_request =
			tw_renderer_modifiers_request;
		server->dma_engine->format_request.callback =
			server->wlr_renderer;
	}

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
