/*
 * data_dnd.c - taiwins server wl_data_device drag-n-drop implemenation
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

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/subsurface.h>
#include <taiwins/objects/cursor.h>
#include <wayland-util.h>

#include "data_internal.h"

/* the second place for creating new data offer */
static void
dnd_pointer_enter(struct tw_seat_pointer_grab *grab,
                  struct wl_resource *surface, double sx, double sy)
{
	struct tw_seat *seat = grab->seat;
	struct tw_pointer *pointer = &seat->pointer;
	struct tw_data_drag *drag =
		wl_container_of(grab, drag, pointer_grab);
	struct tw_data_device *data_device =
		wl_container_of(drag, data_device, drag);
	struct wl_resource *resource =
		tw_data_device_find_client(data_device, surface);
	struct wl_resource *prev_resource =
		tw_data_device_find_client(data_device,
		                           pointer->focused_surface);
	pointer->focused_client =
		tw_seat_client_find(seat, wl_resource_get_client(surface));
	pointer->focused_surface = surface;
	drag->dest_device_resource = resource;

	//we should have
	if (drag->source != NULL) {
		uint32_t serial;
		struct wl_resource *offer;

		//creating data_offers
		drag->source->accepted = false;
		//TODO: idealy create data offer for all the surface that
		offer = tw_data_device_create_data_offer(resource,
		                                         drag->source);
		serial = wl_display_next_serial(seat->display);
		wl_data_device_send_leave(prev_resource);
		wl_data_device_send_enter(resource, serial, surface,
		                          wl_fixed_from_double(sx),
		                          wl_fixed_from_double(sy),
		                          offer);
	}
}

static void
dnd_pointer_button(struct tw_seat_pointer_grab *grab,
                   uint32_t time_msec, uint32_t button,
                   enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_data_drag *drag =
		wl_container_of(grab, drag, pointer_grab);
	struct tw_data_source *source = drag->source;
	struct tw_data_offer *offer = source ? &source->offer : NULL;

	if (state != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	if (offer && offer->source->accepted &&
	    offer->source->selected_dnd_action) {
		//drop send to the one with the offer
		wl_data_device_send_drop(drag->dest_device_resource);
		source = offer->source;

		if (wl_resource_get_version(offer->source->resource) >=
		    WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
			tw_data_source_send_dnd_drop(source);
	} else if (source) {
		tw_data_source_send_cancel(source);
	}
	tw_pointer_end_grab(pointer, grab);
}

static void
dnd_pointer_motion(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                   double sx, double sy)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_data_drag *drag =
		wl_container_of(grab, drag, pointer_grab);
	struct tw_data_device *device =
		wl_container_of(drag, device, drag);
	struct wl_resource *device_resource =
		tw_data_device_find_client(device, pointer->focused_surface);

	wl_data_device_send_motion(device_resource, time_msec,
	                           wl_fixed_from_double(sx),
	                           wl_fixed_from_double(sy));
}

static void
dnd_pointer_cancel(struct tw_seat_pointer_grab *grab)
{
	struct tw_seat *seat = grab->seat;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct wl_resource *surface_resource = pointer->focused_surface;
	struct tw_data_drag *drag = wl_container_of(grab, drag, pointer_grab);
	struct tw_surface *surface = (surface_resource) ?
		tw_surface_from_resource(surface_resource) : NULL;

	tw_keyboard_end_grab(&seat->keyboard, &drag->keyboard_grab);
	if (surface) {
		float sx, sy;
		struct tw_cursor *cursor = grab->seat->cursor;
		tw_surface_to_local_pos(surface, cursor->x, cursor->y,
		                        &sx, &sy);
		tw_pointer_notify_enter(pointer, surface_resource, sx, sy);
	}
}

static const struct tw_pointer_grab_interface dnd_pointer_grab_impl = {
	.enter = dnd_pointer_enter,
	.motion = dnd_pointer_motion,
	.button = dnd_pointer_button,
	.cancel = dnd_pointer_cancel,
};

static const struct tw_keyboard_grab_interface dnd_keyboard_grab_impl = {
	.enter = tw_keyboard_default_enter,
};

static void
notify_data_drag_source_destroy(struct wl_listener *listener, void *data)
{
	struct tw_data_drag *drag =
		wl_container_of(listener, drag, source_destroy_listener);

	wl_list_remove(&drag->source_destroy_listener.link);
	drag->source = NULL;
}

/*****************************************************************************
 * dnd surface
 *****************************************************************************/

static void
commit_dnd_icon_surface(struct tw_surface *surface)
{
	//TODO; this is hack using the attach position
	struct tw_subsurface *sub = surface->role.commit_private;
	tw_subsurface_update_pos(sub,
	                         sub->surface->current->dx,
	                         sub->surface->current->dy);
}

static void
notify_icon_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct tw_subsurface *sub =
		wl_container_of(listener, sub, surface_destroyed);
	struct tw_data_drag *drag =
		wl_container_of(sub, drag, subsurf);
	tw_subsurface_fini(sub);
}

static void
data_drag_set_icon(struct tw_data_drag *drag, struct tw_cursor *cursor,
                   struct wl_resource *icon_res)
{
	struct tw_surface *icon = icon_res ?
		tw_surface_from_resource(icon_res) : NULL;
	struct tw_subsurface *subsurf = &drag->subsurf;
	struct tw_surface *parent = cursor->curr_surface;

	if (!tw_surface_assign_role(icon, &tw_data_drag_role, subsurf)) {
		wl_resource_post_error(icon_res, WL_DATA_DEVICE_ERROR_ROLE,
		                       "wl_surface@%d has another role",
		                       wl_resource_get_id(icon_res));
		return;
	}

	if (!icon || !parent)
		return;
	tw_subsurface_init(subsurf, NULL, icon, parent,
	                   notify_icon_surface_destroyed);
	subsurf->pos = TW_SUBSURFACE_BELOW;
	subsurf->sync = false;
}

bool
tw_data_source_start_drag(struct tw_data_drag *drag,
                          struct wl_resource *device_resource,
                          struct wl_resource *icon,
                          struct tw_data_source *source, struct tw_seat *seat)
{
	if (!(seat->capabilities & WL_SEAT_CAPABILITY_POINTER))
		return false;

	if (seat->pointer.grab != &seat->pointer.default_grab)
		return false;

	drag->pointer_grab.data = device_resource;
	drag->pointer_grab.impl = &dnd_pointer_grab_impl;
	drag->keyboard_grab.data = device_resource;
	drag->keyboard_grab.impl = &dnd_keyboard_grab_impl;
	drag->source = source;
	drag->dest_device_resource = device_resource;

	tw_signal_setup_listener(&drag->source->destroy_signal,
	                         &drag->source_destroy_listener,
	                         notify_data_drag_source_destroy);

	//we need to trigger a enter event
	tw_pointer_start_grab(&seat->pointer, &drag->pointer_grab,
	                      TW_DATA_DND_GRAB_ORDER);
	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		tw_keyboard_start_grab(&seat->keyboard, &drag->keyboard_grab,
		                       TW_DATA_DND_GRAB_ORDER);

	data_drag_set_icon(drag, seat->cursor, icon);

	return true;
}

struct tw_surface_role tw_data_drag_role = {
	.commit = commit_dnd_icon_surface,
	.name = "drag-and-drop",
	.link.prev = &tw_data_drag_role.link,
	.link.next = &tw_data_drag_role.link,
};
