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

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "data_internal.h"

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

static const struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = tw_resource_destroy_common,
	.set_actions = data_source_set_actions,
};

static void
wl_data_source_target(struct tw_data_source *source, const char *mime)
{
	wl_data_source_send_target(source->resource, mime);
}

static void
wl_data_source_send(struct tw_data_source *source, const char *mime, int fd)
{
	wl_data_source_send_send(source->resource, mime, fd);
}

static void
wl_data_source_cancel(struct tw_data_source *source)
{
	wl_data_source_send_cancelled(source->resource);
}

static void
wl_data_source_dnd_drop(struct tw_data_source *source)
{
	wl_data_source_send_dnd_drop_performed(source->resource);
}

static void
wl_data_source_dnd_finish(struct tw_data_source *source)
{
	wl_data_source_send_dnd_finished(source->resource);
}

static void
wl_data_source_action(struct tw_data_source *source, uint32_t action)
{
	wl_data_source_send_action(source->resource, action);
}

static const struct tw_data_source_impl data_source_cb = {
	.target = wl_data_source_target,
	.send = wl_data_source_send,
	.cancel = wl_data_source_cancel,
	.dnd_drop = wl_data_source_dnd_drop,
	.dnd_finish = wl_data_source_dnd_finish,
	.action = wl_data_source_action,
};

static void
destroy_data_source(struct wl_resource *resource)
{
	struct tw_data_source *source = tw_data_source_from_resource(resource);

	tw_data_source_fini(source);
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

	tw_data_source_init(source, resource, &data_source_cb);
	wl_resource_set_implementation(resource, &data_source_impl, source,
	                               destroy_data_source);
	return source;
}

WL_EXPORT void
tw_data_source_init(struct tw_data_source *source, struct wl_resource *res,
                    const struct tw_data_source_impl *impl)
{
	source->resource = res;
	source->selection_source = false;
	source->impl = impl;
	source->actions = 0;

	wl_array_init(&source->mimes);
	wl_signal_init(&source->destroy_signal);
	tw_data_offer_init(&source->offer, source);
}

WL_EXPORT void
tw_data_source_fini(struct tw_data_source *source)
{
	char **mime_type;

	wl_array_for_each(mime_type, &source->mimes)
		free(*mime_type);
	wl_array_release(&source->mimes);
	wl_signal_emit(&source->destroy_signal, source);
	source->actions = 0;
}
