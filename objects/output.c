/*
 * output.c - taiwins wl_output protocol implementation
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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/output.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>

static const struct wl_output_interface output_impl;

static inline struct tw_output *
tw_output_from_resource(struct wl_resource *resource)
{
	wl_resource_instance_of(resource, &wl_output_interface,
	                        &output_impl);
	return wl_resource_get_user_data(resource);
}

static void
tw_output_send_config(struct wl_resource *resource)
{
	struct tw_output *output = tw_output_from_resource(resource);
	if (resource) {
		wl_output_send_geometry(resource, output->x, output->y,
		                        output->geometry.phyw,
		                        output->geometry.phyh,
		                        output->geometry.subpixel,
		                        output->geometry.make,
		                        output->geometry.model,
		                        output->geometry.transform);
		wl_output_send_mode(resource, output->mode.flags,
		                    output->mode.width, output->mode.height,
		                    output->mode.refresh);
		wl_output_send_scale(resource, output->scale);
		wl_output_send_done(resource);
	}
}

void
tw_output_set_name(struct tw_output *output, const char *name)
{
	strncpy(output->name, name, 31);
}

void
tw_output_set_scale(struct tw_output *output, uint32_t scale)
{
	if (!scale) {
		tw_logl("invalid scale %d for tw_output", scale);
		return;
	}
	output->scale = scale;
}

void
tw_output_set_coord(struct tw_output *output, int x, int y)
{
	output->x = x;
	output->y = y;
}
void
tw_output_set_geometry(struct tw_output *output,
                       int physical_w, int physical_h,
                       char *make, char *model,
                       enum wl_output_subpixel subpixel,
                       enum wl_output_transform transform)
{
	free(output->geometry.model);
	free(output->geometry.make);

	output->geometry.phyw = physical_w;
	output->geometry.phyh = physical_h;
	output->geometry.make = make ? strdup(make) : NULL;
	output->geometry.model = model ? strdup(model) : NULL;
	output->geometry.subpixel = subpixel;
	output->geometry.transform = transform;
}

void
tw_output_set_mode(struct tw_output *output, uint32_t mode_flags,
                   int width, int height, int refresh)
{
	uint32_t all_flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;

        if (!(mode_flags & all_flags)) {
		tw_logl("invalid wl_output model flags %d", mode_flags);
		return;
	}
	output->mode.flags = mode_flags;
	output->mode.width = width;
	output->mode.height = height;
	output->mode.refresh = refresh;

}

void
tw_output_send_clients(struct tw_output *output)
{
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources)
		tw_output_send_config(resource);

}

static void
handle_output_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_output_interface output_impl = {
	.release = handle_output_release,
};

static void
destroy_output_resource(struct wl_resource *resource)
{
	wl_resource_set_user_data(resource, NULL);
	tw_reset_wl_list(wl_resource_get_link(resource));
}

static void
bind_output(struct wl_client *client, void *data, uint32_t version,
            uint32_t id)
{
	struct tw_output *output = data;
	struct wl_resource *resource =
		wl_resource_create(client, &wl_output_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &output_impl, data,
	                               destroy_output_resource);
	wl_list_insert(output->resources.prev, wl_resource_get_link(resource));
	tw_output_send_config(resource);
}

static void
notify_output_display_destroy(struct wl_listener *listener, void *data)
{
	struct wl_resource *res;
	struct tw_output *output =
		wl_container_of(listener, output, display_destroy_listener);

        wl_resource_for_each(res, &output->resources)
		wl_resource_set_user_data(res, NULL);
	wl_global_destroy(output->global);
	wl_list_remove(&output->display_destroy_listener.link);
	free(output);

}

struct tw_output *
tw_output_create(struct wl_display *display)
{
	struct tw_output *output = calloc(1, sizeof(*output));
	if (!output)
		return NULL;
	output->global =
		wl_global_create(display, &wl_output_interface, 1, output,
		                 bind_output);
	if (!output->global) {
		free(output);
		return NULL;
	}
	output->display = display;
	output->scale = 1;
	output->mode.flags = WL_OUTPUT_MODE_CURRENT;
	output->geometry.subpixel = WL_OUTPUT_SUBPIXEL_NONE;
	output->geometry.transform = WL_OUTPUT_TRANSFORM_NORMAL;
	wl_list_init(&output->resources);
	tw_set_display_destroy_listener(display,
	                                &output->display_destroy_listener,
	                                notify_output_display_destroy);
	return output;
}
