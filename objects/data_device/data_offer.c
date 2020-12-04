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

#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/utils.h>

#include "data_internal.h"

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
	const char **p;

	if (!offer || !offer->source)
		return;
	if (!mime_type) {
		offer->source->accepted = false;
		return;
	}

	wl_array_for_each(p, &offer->source->mimes) {
		if (mime_type && !strcmp(*p, mime_type)) {
			wl_data_source_send_target(offer->source->resource,
			                           mime_type);
			offer->source->accepted = true;
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

        if (!offer || !offer->source)
		return;

	//it is either we do not check at all or we verify if offer source is
	//linked
	wl_data_source_send_send(offer->source->resource, mime_type, fd);
	close(fd);
}

static void
data_offer_finish(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_data_offer *offer = tw_data_offer_from_resource(resource);
	struct tw_data_source *source = offer->source;

	if (!offer || !offer->source)
		return;

	if (source->selection_source) {
		wl_resource_post_error(resource,
		                       WL_DATA_OFFER_ERROR_INVALID_FINISH,
		                       "finish only works for drag n drop");
		return;
	}
	if (!source->accepted) {
		wl_resource_post_error(resource,
		                       WL_DATA_OFFER_ERROR_INVALID_FINISH,
		                       "permature finish request");
		return;
	}
	if (source->selected_dnd_action ==
	    WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
		wl_resource_post_error(resource,
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

	if (!offer || !offer->source)
		return;

	if (dnd_actions && !(dnd_actions & TW_DATA_DEVICE_ACCEPT_ACTIONS)) {
		wl_resource_post_error(resource,
		                       WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
		                       "action mask %d invalid", dnd_actions);
		return;
	}
	if (preferred_action &&
	    (!(preferred_action && dnd_actions) &&
	     !(supported_prefer_action(preferred_action)))) {
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
	if (wl_resource_get_version(resource) >=
		WL_DATA_OFFER_ACTION_SINCE_VERSION)
		wl_data_offer_send_action(resource,
		                          determined_action);
	offer->source->selected_dnd_action = determined_action;
}

static const struct wl_data_offer_interface data_offer_impl = {
	.accept = data_offer_accept,
	.receive = data_offer_receive,
	.destroy = tw_resource_destroy_common,
	.finish = data_offer_finish,
	.set_actions = data_offer_set_actions,
};

static void
destroy_data_offer_resource(struct wl_resource *resource)
{
	tw_reset_wl_list(wl_resource_get_link(resource));
	wl_resource_set_user_data(resource, NULL);
}


static void
notify_offer_source_destroy(struct wl_listener *listener, void *data)
{
	struct tw_data_offer *offer =
		wl_container_of(listener, offer, source_destroy_listener);
	struct wl_resource *r, *tmp;

	wl_resource_for_each_safe(r, tmp, &offer->resources)
		destroy_data_offer_resource(r);
}

void
tw_data_offer_init(struct tw_data_offer *offer,
                   struct tw_data_source *source)
{
	wl_list_init(&offer->resources);
	offer->source = source;
	tw_signal_setup_listener(&source->destroy_signal,
	                         &offer->source_destroy_listener,
	                         notify_offer_source_destroy);
}

void
tw_data_offer_add_resource(struct tw_data_offer *offer,
                           struct wl_resource *resource,
                           struct wl_resource *device_resource)
{
	const char **p;
	uint32_t version = wl_resource_get_version(resource);

	wl_list_insert(offer->resources.prev,
	               wl_resource_get_link(resource));
	wl_resource_set_implementation(resource, &data_offer_impl,
	                               offer, destroy_data_offer_resource);

	//offer is added, advertise the possible mimetypes and actions.
	wl_data_device_send_data_offer(device_resource, resource);
	wl_array_for_each(p, &offer->source->mimes)
		wl_data_offer_send_offer(resource, *p);
	if (offer->source->actions &&
	    version >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
		wl_data_offer_send_source_actions(resource,
		                                  offer->source->actions);
}

void
tw_data_offer_set_source_actions(struct tw_data_offer *offer,
                                 uint32_t dnd_actions)
{
	struct wl_resource *r;
	wl_resource_for_each(r, &offer->resources) {
		if (wl_resource_get_version(r) >=
		    WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
			wl_data_offer_send_source_actions(r, dnd_actions);
	}
}
