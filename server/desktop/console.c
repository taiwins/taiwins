/*
 * console.c - taiwins console implementation
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

#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <sequential.h>
#include <wayland-server.h>
#include <wayland-taiwins-console-server-protocol.h>

#include "../taiwins.h"
#include "shell.h"

/**
 * this struct handles the request and sends the event from
 * tw_console.
 */
struct console {
	// client  info
	char path[256];
	struct wl_client *client;
	struct wl_resource *resource;
	pid_t pid; uid_t uid; gid_t gid;

	struct weston_compositor *compositor;
	struct shell *shell;
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener close_console_listener;
	struct wl_global *global;
	struct tw_subprocess process;
};

static struct console s_console;

static void
console_surface_destroy_cb(struct wl_listener *listener, void *data)
{
	struct console *console = container_of(listener, struct console, close_console_listener);
	console->surface = NULL;
}


static void
close_console(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *wl_buffer,
		       uint32_t exec_id)
{
	fprintf(stderr, "the console client is %p\n", client);
	struct console *lch = (struct console *)wl_resource_get_user_data(resource);
	lch->decision_buffer = wl_shm_buffer_get(wl_buffer);
	taiwins_console_send_exec(resource, exec_id);
}


static void
set_console(struct wl_client *client,
	     struct wl_resource *resource,
	     uint32_t ui_elem,
	     struct wl_resource *wl_surface)
{
	struct console *lch = wl_resource_get_user_data(resource);
	shell_create_ui_elem(lch->shell, client, ui_elem, wl_surface,
			     100, 100, TAIWINS_UI_TYPE_WIDGET);
	lch->surface = tw_surface_from_resource(wl_surface);
	wl_resource_add_destroy_listener(wl_surface, &lch->close_console_listener);
}


static struct taiwins_console_interface console_impl = {
	.launch = set_console,
	.submit = close_console
};


static void
unbind_console(struct wl_resource *r)
{
	fprintf(stderr, "console closed.\n");
	struct console *console = wl_resource_get_user_data(r);
	console->client = NULL;
	console->resource = NULL;
	console->pid = console->uid = console->gid = -1;
	//I don't think I need to release the wl_resource
}


static void
bind_console(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct console *console = data;
	struct wl_resource *wl_resource = wl_resource_create(client, &taiwins_console_interface,
							  TWDESKP_VERSION, id);

	uid_t uid; gid_t gid; pid_t pid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (console->client &&
	    (uid != console->uid || pid != console->pid || gid != console->gid)) {
		wl_resource_post_error(wl_resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "client %d is not un atherized console", id);
		wl_resource_destroy(wl_resource);
		return;
	}
	console->client = client;
	console->resource = wl_resource;
	wl_resource_set_implementation(wl_resource, &console_impl, console, unbind_console);
}


static void
launch_console_client(void *data)
{
	struct console *console = data;

	console->process.user_data = console;
	console->process.chld_handler = NULL;
	console->client = tw_launch_client(console->compositor, console->path,
	                                   &console->process);

	wl_client_get_credentials(console->client, &console->pid,
	                          &console->uid,
	                          &console->gid);
}

void
tw_console_start_client(struct console *console)
{
	if (!console || !console->client) //we do not have a console
		return;
	if ((console->surface) &&  //or the console is active
	    wl_list_length(&console->surface->views))
		return;

	taiwins_console_send_start(console->resource,
				    wl_fixed_from_int(600),
				    wl_fixed_from_int(300),
				    wl_fixed_from_int(1));
}

static void
end_console(struct wl_listener *listener, void *data)
{
	struct console *c =
		container_of(listener, struct console,
			     compositor_destroy_listener);
	wl_global_destroy(c->global);
}


struct console *
tw_setup_console(struct weston_compositor *compositor, const char *path,
                 struct shell *shell)
{
	if(path && (strlen(path) +1 > NUMOF(s_console.path)))
		return NULL;

	s_console.surface = NULL;
	s_console.resource = NULL;
	s_console.compositor = compositor;
	s_console.shell = shell;
	s_console.global =
		wl_global_create(compositor->wl_display,
		                 &taiwins_console_interface,
		                 TWDESKP_VERSION,
		                 &s_console,
				 bind_console);

	if (path) {
		strcpy(s_console.path, path);
		struct wl_event_loop *loop =
			wl_display_get_event_loop(compositor->wl_display);
		wl_event_loop_add_idle(loop, launch_console_client, &s_console);
	}

	//close close
	wl_list_init(&s_console.close_console_listener.link);
	s_console.close_console_listener.notify = console_surface_destroy_cb;
	//where is the signal for console close???

	//destroy globals
	wl_list_init(&s_console.compositor_destroy_listener.link);
	s_console.compositor_destroy_listener.notify = end_console;
	wl_signal_add(&compositor->destroy_signal,
		      &s_console.compositor_destroy_listener);

	return &s_console;
}
