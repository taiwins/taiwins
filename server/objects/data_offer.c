/*
 * data_offer.c - taiwins server wl_data_offer implementation
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
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <ctypes/helpers.h>

#include "data_device.h"

static const struct wl_data_offer_interface data_offer_impl;

static inline bool
supported_prefer_action(uint32_t preferred_action)
{
	return preferred_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY ||
		preferred_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ||
		preferred_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
}

static struct tw_data_offer *
tw_data_offer_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wl_data_offer_interface,
	                               &data_offer_impl));
	return wl_resource_get_user_data(resource);
}

static void
data_offer_accept(struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t serial,
                  const char *mime_type)
{
	struct tw_data_offer *offer = tw_data_offer_from_resource(resource);
	const char *p;

	wl_array_for_each(p, &offer->source->mimes) {
		if (!strcmp(p, mime_type)) {
			wl_data_source_send_target(offer->source->resource,
			                           mime_type);
			offer->source->accepted = true;
			//TODO: setting the accepted in source
			return;
		}
	}
	wl_data_source_send_cancelled(offer->source->resource);
}

static void
data_offer_receive(struct wl_client *client,
                   struct wl_resource *resource,
                   const char *mime_type,
                   int32_t fd)
{
	struct tw_data_offer *offer = tw_data_offer_from_resource(resource);

	//it is either we do not check at all or we verify if offer source is
	//linked
	wl_data_source_send_send(offer->source->resource, mime_type, fd);
}

static void
data_offer_handle_destroy(struct wl_client *client,
                          struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
data_offer_finish(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_data_offer *offer = tw_data_offer_from_resource(resource);
	struct tw_data_source *source = offer->source;

	if (source->selection_source) {
		wl_resource_post_error(offer->resource,
		                       WL_DATA_OFFER_ERROR_INVALID_FINISH,
		                       "finish only works for drag n drop");
		return;
	}
	if (!source->accepted) {
		wl_resource_post_error(offer->resource,
		                       WL_DATA_OFFER_ERROR_INVALID_FINISH,
		                       "permature finish request");
		return;
	}
	if (source->selected_dnd_action ==
	    WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
		wl_resource_post_error(offer->resource,
		                       WL_DATA_OFFER_ERROR_INVALID_FINISH,
		                       "offer finished with invalid action");
		return;
	}
	//different versions have different handling though.
	offer->finished = true;

	wl_data_source_send_dnd_finished(offer->source->resource);
}

static void
data_offer_set_actions(struct wl_client *client,
                       struct wl_resource *resource,
                       uint32_t dnd_actions,
                       uint32_t preferred_action)
{
	uint32_t determined_action;
	struct tw_data_offer *offer = tw_data_offer_from_resource(resource);
	if (!(dnd_actions & preferred_action) ||
	    !(dnd_actions & (TW_DATA_DEVICE_ACCEPT_ACTIONS)) ||
	    !(supported_prefer_action(preferred_action))) {
		wl_resource_post_error(resource,
		                       WL_DATA_OFFER_ERROR_INVALID_ACTION,
		                       "requested dnd actions not supported.");
		return;
	}
	if (offer->source->selection_source) {
		wl_resource_post_error(resource,
		                       WL_DATA_OFFER_ERROR_INVALID_OFFER,
		                       "set-action only works on "
		                       "drag-n-drop offers");
		return;
	}

	if (!(dnd_actions & offer->source->actions)) {
		wl_data_source_send_cancelled(offer->source->resource);
		return;
	}

	//okay, this is the determined action for the offer.
	determined_action = preferred_action;
	if (wl_resource_get_version(offer->source->resource) >=
		WL_DATA_SOURCE_ACTION_SINCE_VERSION)
		wl_data_source_send_action(offer->source->resource,
		                           determined_action);
	if (wl_resource_get_version(offer->resource) >=
		WL_DATA_OFFER_ACTION_SINCE_VERSION)
		wl_data_offer_send_action(offer->resource,
		                          determined_action);
	offer->source->selected_dnd_action = determined_action;
}

static const struct wl_data_offer_interface data_offer_impl = {
	.accept = data_offer_accept,
	.receive = data_offer_receive,
	.destroy = data_offer_handle_destroy,
	.finish = data_offer_finish,
	.set_actions = data_offer_set_actions,
};

static void
notify_offer_source_destroy(struct wl_listener *listener, void *data)
{
	struct tw_data_offer *offer =
		container_of(listener, struct tw_data_offer,
		             source_destroy_listener);
	wl_list_remove(&offer->source_destroy_listener.link);
	//TODO: source does not exist, we shall be gone as well?
	wl_resource_destroy(offer->resource);
}

static void
destroy_data_offer_resource(struct wl_resource *resource)
{
	struct tw_data_offer *offer = wl_resource_get_user_data(resource);
	wl_list_remove(&offer->source_destroy_listener.link);
	free(offer);
}

struct tw_data_offer *
tw_data_offer_create(struct wl_resource *resource,
                     struct tw_data_source *source)
{
	struct tw_data_offer *offer = calloc(1, sizeof(struct tw_data_offer));
	if (offer)
		return NULL;
	wl_resource_set_implementation(resource, &data_offer_impl, offer,
	                               destroy_data_offer_resource);
	offer->resource = resource;
	wl_list_init(&offer->source_destroy_listener.link);
	offer->source_destroy_listener.notify = notify_offer_source_destroy;
	if (source) {
		offer->source = source;
		wl_signal_add(&offer->source->destroy_signal,
		              &offer->source_destroy_listener);
	}
	return offer;
}