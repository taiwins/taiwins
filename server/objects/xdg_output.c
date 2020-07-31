/*
 * xdg_output.c - taiwins xdg-output protocol implementation
 *
 * Copyright (c) 2020 Xichen Zhou
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

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wayland-xdg-output-server-protocol.h>
#include <ctypes/helpers.h>

#include <taiwins/objects/xdg_output.h>


#define XDG_OUTPUT_MAN_VERSION 1
#define XDG_OUTPUT_VERSION 3

static void
handle_output_destroy(struct wl_client *client,
                      struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zxdg_output_v1_interface xdg_output_impl = {
	.destroy = handle_output_destroy,
};

static void
handle_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_get_xdg_output(struct wl_client *client,
                      struct wl_resource *resource,
                      uint32_t id,
                      struct wl_resource *output)
{
	struct tw_xdg_output_info_event event = {
		.wl_output = output,
	};
	struct tw_xdg_output_manager *manager =
		wl_resource_get_user_data(resource);
	struct wl_resource *output_resource =
		wl_resource_create(client, &zxdg_output_v1_interface,
		                   XDG_OUTPUT_VERSION, id);
	if (!output_resource) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(output_resource, &xdg_output_impl,
	                               NULL, NULL);

	wl_signal_emit(&manager->xdg_output_requested, &event);
	if (event.name && event.width && event.height) {
		zxdg_output_v1_send_name(output_resource, event.name);
		zxdg_output_v1_send_logical_position(output_resource,
		                                     event.x, event.y);
		zxdg_output_v1_send_logical_size(output_resource,
		                                 event.width, event.height);
		if (event.desription)
			zxdg_output_v1_send_description(output_resource,
			                                event.desription);
		zxdg_output_v1_send_done(output_resource);
	}
}

static const struct zxdg_output_manager_v1_interface xdg_output_man_impl = {
	.destroy = handle_manager_destroy,
	.get_xdg_output = handle_get_xdg_output,
};

static void
destroy_xdg_output_manager_resource(struct wl_resource *resource)
{

}

static void
bind_xdg_output_manager(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id)
{
	struct tw_xdg_output_manager *manager = data;
	struct wl_resource *res =
		wl_resource_create(client, &zxdg_output_manager_v1_interface,
		                   version, id);
	if (!res)  {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &xdg_output_man_impl,
	                               manager,
	                               destroy_xdg_output_manager_resource);
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xdg_output_manager *manager =
		container_of(listener, struct tw_xdg_output_manager,
		             display_destroy_listener);
	wl_global_destroy(manager->global);
}

bool
tw_xdg_output_manager_init(struct tw_xdg_output_manager *manager,
                           struct wl_display *display)
{
	manager->global =
		wl_global_create(display, &zxdg_output_manager_v1_interface,
		                 XDG_OUTPUT_MAN_VERSION, manager,
		                 bind_xdg_output_manager);
	if (!manager->global)
		return false;
	manager->display = display;
	wl_list_init(&manager->display_destroy_listener.link);
	manager->display_destroy_listener.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display,
	                                &manager->display_destroy_listener);
	wl_signal_init(&manager->xdg_output_requested);
	return true;
}

struct tw_xdg_output_manager *
tw_xdg_output_manager_create_global(struct wl_display *display)
{
	static struct tw_xdg_output_manager s_xdg_output_manager = {0};

	if (!tw_xdg_output_manager_init(&s_xdg_output_manager, display))
		return false;

	return &s_xdg_output_manager;
}
