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


#include "data_device.h"
#include "seat.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

// the data device has a few methods you need to call,
// enter/leave/motion/drop/

// for client, a new data offer is created when (1) keyboard focused on a
// different surface(through wl_data_device_send_selection). The surface enter
// event, (2) surface enter event(also remove the ones in leaving event).

static void
dnd_pointer_enter(struct tw_seat_pointer_grab *grab,
                  struct wl_resource *surface, double sx, double sy)
{
	uint32_t serial;
	struct tw_seat *seat = grab->seat;
	struct tw_data_device *device = grab->data;
	// TODO maybe execute normal enter?

	if (!device->source_set ||
	    surface == device->source_set->drag_origin_surface)
		return;
	if (device->offer_set) {
		if (device->offer_set->current_surface == surface)
			return;
		else
			wl_data_device_send_leave(device->resource);
	} else {
		device->offer_set =
			tw_data_device_create_data_offer(device, surface);
	}
	serial = wl_display_next_serial(seat->display);
	wl_data_device_send_enter(device->resource, serial, surface,
	                          wl_fixed_from_double(sx),
	                          wl_fixed_from_double(sy),
	                          device->offer_set->resource);

	//TODO I am not destroying the data_offer though,
	// it would be destroyed on client destroyed.
}


static void
dnd_pointer_button(struct tw_seat_pointer_grab *grab,
                   uint32_t time_msec, uint32_t button,
                   enum wl_pointer_button_state state)
{
	struct tw_data_device *device = grab->data;
	struct tw_data_offer *offer = device->offer_set;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_data_source *source;

	if (state != WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	if (offer && offer->source->accepted &&
	    offer->source->selected_dnd_action) {
		wl_data_device_send_drop(device->resource);
		source = offer->source;

		if (wl_resource_get_version(offer->source->resource) >=
		    WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
			wl_data_source_send_dnd_drop_performed(
				source->resource);
	}

	if (pointer->btn_count == 0) {
		tw_pointer_end_grab(&grab->seat->pointer);
	}
}

void
dnd_pointer_motion(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                       double sx, double sy)
{
	struct tw_data_device *device = grab->data;

	wl_data_device_send_motion(device->resource, time_msec,
	                           wl_fixed_from_double(sx),
	                           wl_fixed_from_double(sy));
}

static const struct tw_pointer_grab_interface dnd_pointer_grab_impl = {
	.enter = dnd_pointer_enter,
	.motion = dnd_pointer_motion,
	.button = dnd_pointer_button,
};

bool
tw_data_source_start_drag(struct tw_data_device *device,
                          struct tw_seat *seat)
{
	static struct tw_seat_pointer_grab dnd_pointer_grab;
	//static struct tw_seat_touch_grab dnd_touch_grab;

	if (!(seat->capabilities & WL_SEAT_CAPABILITY_POINTER))
		return false;

	if (seat->pointer.grab != &seat->pointer.default_grab)
		return false;
	dnd_pointer_grab.seat = seat;
	dnd_pointer_grab.data = device;
	dnd_pointer_grab.impl = &dnd_pointer_grab_impl;

	tw_pointer_start_grab(&seat->pointer, &dnd_pointer_grab);

	return true;
}
