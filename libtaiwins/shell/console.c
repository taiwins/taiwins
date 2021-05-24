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

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <pixman.h>
#include <wayland-taiwins-console-server-protocol.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/utils.h>
#include <ctypes/sequential.h>

#include <taiwins/output_device.h>
#include <taiwins/shell.h>
#include <taiwins/engine.h>

#define CONSOLE_WIDTH  600
#define CONSOLE_HEIGHT 300

/**
 * this struct handles the request and sends the event from
 * tw_console.
 */
static struct tw_console {
	// client  info
	char path[256];
	struct wl_client *client;
	struct wl_resource *resource;
	pid_t pid; uid_t uid; gid_t gid;
	struct tw_shell *shell;
	struct wl_display *display;
	struct tw_engine *engine;

	struct wl_shm_buffer *decision_buffer;
	struct tw_surface *surface;
	struct wl_listener display_destroy_listener;
	struct wl_listener close_console_listener;
	struct wl_global *global;
	struct tw_subprocess process;
} s_console;

static struct taiwins_console_interface console_impl;

/* static inline struct tw_console * */
/* tw_console_from_resource(struct wl_resource *resource) */
/* { */
/*	assert(wl_resource_instance_of(resource, &taiwins_console_interface, */
/*	                               &console_impl)); */
/*	return wl_resource_get_user_data(resource); */
/* } */

static void
notify_console_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_console *console =
		container_of(listener, struct tw_console,
		             close_console_listener);
	tw_reset_wl_list(&listener->link);
	console->surface = NULL;
}


static void
close_console(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *wl_buffer,
		       uint32_t exec_id)
{
	fprintf(stderr, "the console client is %p\n", client);
	struct tw_console *lch =
		(struct tw_console *)wl_resource_get_user_data(resource);
	lch->decision_buffer = wl_shm_buffer_get(wl_buffer);
	taiwins_console_send_exec(resource, exec_id);
}


static void
set_console(struct wl_client *client,
            struct wl_resource *resource,
            uint32_t ui_elem,
            struct wl_resource *wl_surface,
            struct wl_resource *wl_seat)
{
	struct tw_console *console = wl_resource_get_user_data(resource);
	struct tw_engine_output *output =
		tw_engine_get_focused_output(console->engine);
	struct tw_seat *seat = tw_seat_from_resource(wl_seat);
	struct tw_keyboard *keyboard = &seat->keyboard;
	pixman_rectangle32_t geo = tw_output_device_geometry(output->device);
	int32_t sx = geo.width/2 - CONSOLE_WIDTH/2;

	tw_shell_create_ui_elem(console->shell, client, output, ui_elem,
	                        wl_surface, sx, 100, TAIWINS_UI_TYPE_WIDGET);
	console->surface = tw_surface_from_resource(wl_surface);
	wl_resource_add_destroy_listener(wl_surface,
	                                 &console->close_console_listener);
	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		tw_keyboard_notify_enter(keyboard, wl_surface, NULL, 0);
}


static struct taiwins_console_interface console_impl = {
	.launch = set_console,
	.submit = close_console
};


static void
unbind_console(struct wl_resource *r)
{
	fprintf(stderr, "console closed.\n");
	struct tw_console *console = wl_resource_get_user_data(r);
	console->client = NULL;
	console->resource = NULL;
	console->pid = console->uid = console->gid = -1;
	//I don't think I need to release the wl_resource
}


static void
bind_console(struct wl_client *client, void *data, uint32_t version,
             uint32_t id)
{
	struct tw_console *console = data;
	struct wl_resource *wl_resource =
		wl_resource_create(client, &taiwins_console_interface,
		                   TWDESKP_VERSION, id);

	uid_t uid; gid_t gid; pid_t pid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (console->client &&
	    (uid != console->uid || pid != console->pid ||
	     gid != console->gid)) {
		wl_resource_post_error(wl_resource,
		                       WL_DISPLAY_ERROR_INVALID_OBJECT,
		                       "client %d is not un atherized console",
		                       id);
		wl_resource_destroy(wl_resource);
		return;
	}
	console->client = client;
	console->resource = wl_resource;
	wl_resource_set_implementation(wl_resource, &console_impl,
	                               console, unbind_console);
}


static void
launch_console_client(void *data)
{
	struct tw_console *console = data;

	console->process.user_data = console;
	console->process.chld_handler = NULL;
	console->client = tw_launch_client(console->display, console->path,
	                                   &console->process);

	wl_client_get_credentials(console->client, &console->pid,
	                          &console->uid,
	                          &console->gid);
}

WL_EXPORT void
tw_console_start_client(struct tw_console *console)
{
	if (!console || !console->client) //we do not have a console
		return;
	if (console->surface) //if already launched.
		return;

	taiwins_console_send_start(console->resource,
				    wl_fixed_from_int(CONSOLE_WIDTH),
				    wl_fixed_from_int(CONSOLE_HEIGHT),
				    wl_fixed_from_int(1));
}

static void
end_console(struct wl_listener *listener, void *data)
{
	struct tw_console *c =
		container_of(listener, struct tw_console,
			     display_destroy_listener);
	wl_global_destroy(c->global);
}


WL_EXPORT struct tw_console *
tw_console_create_global(struct wl_display *display, const char *path,
                         struct tw_engine *engine, struct tw_shell *shell)
{
	if (path && (strlen(path) +1 > NUMOF(s_console.path)))
		return NULL;

	s_console.surface = NULL;
	s_console.resource = NULL;
	s_console.display = display;
	s_console.engine = engine;
	s_console.shell = shell;
	s_console.global =
		wl_global_create(display,
		                 &taiwins_console_interface,
		                 TWDESKP_VERSION,
		                 &s_console,
				 bind_console);

	if (path) {
		strcpy(s_console.path, path);
		struct wl_event_loop *loop =
			wl_display_get_event_loop(display);
		wl_event_loop_add_idle(loop, launch_console_client,
		                       &s_console);
	}

	//close close
	wl_list_init(&s_console.close_console_listener.link);
	s_console.close_console_listener.notify =
		notify_console_surface_destroy;

	//destroy globals
	tw_set_display_destroy_listener(display,
	                                &s_console.display_destroy_listener,
	                                end_console);
	return &s_console;
}
