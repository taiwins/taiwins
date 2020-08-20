/*
 * data_source.c - taiwins server wl_data_source implementation
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


#include <string.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/utils.h>

static const struct wl_data_source_interface data_source_impl;

struct tw_data_source *
tw_data_source_from_resource(struct wl_resource *resource)
{
	wl_resource_instance_of(resource, &wl_data_source_interface,
	                        &data_source_impl);
	return wl_resource_get_user_data(resource);
}

static void
data_source_offer(struct wl_client *client,
                  struct wl_resource *resource,
                  const char *mime_type)
{
	struct tw_data_source *data_source =
		tw_data_source_from_resource(resource);
	char **new_mime_type =
		wl_array_add(&data_source->mimes, sizeof(char *));
	if (new_mime_type)
		*new_mime_type = strdup(mime_type);
}

static void
data_source_set_actions(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t dnd_actions)
{
	struct tw_data_source *data_source =
		tw_data_source_from_resource(resource);
	if (data_source->actions)
		wl_resource_post_error(resource,
		                       WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
		                       "data source action already set.");
	if (dnd_actions && !(dnd_actions & (TW_DATA_DEVICE_ACCEPT_ACTIONS))) {
		wl_resource_post_error(resource,
		                       WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
		                       "the actions are not supported");
		return;
	}
	data_source->actions = dnd_actions;
	tw_data_offer_set_source_actions(&data_source->offer, dnd_actions);
}

static void
data_source_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void
destroy_data_source(struct wl_resource *resource)
{
	char **mime_type;
	struct tw_data_source *source = tw_data_source_from_resource(resource);

	wl_array_for_each(mime_type, &source->mimes)
		free(*mime_type);
	wl_array_release(&source->mimes);
	wl_signal_emit(&source->destroy_signal, source);
	source->actions = 0;
	free(source);
}

struct tw_data_source *
tw_data_source_create(struct wl_client *client, uint32_t id, uint32_t version)
{
	struct tw_data_source *source;
	struct wl_resource *resource = NULL;

	if (!tw_create_wl_resource_for_obj(resource, source, client,
	                                   id, version,
	                                   wl_data_source_interface))
		return NULL;
	source->resource = resource;
	source->selection_source = false;
	wl_resource_set_implementation(resource, &data_source_impl, source,
	                               destroy_data_source);
	wl_array_init(&source->mimes);
	wl_signal_init(&source->destroy_signal);
	source->actions = 0;

	//init an empty offer
	tw_data_offer_init(&source->offer, source);
	return source;
}
