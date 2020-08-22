/*
 * seat_pointer.c - taiwins server wl_pointer implemetation
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

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>

static void
notify_pointer_enter(struct tw_seat_pointer_grab *grab,
                     struct wl_resource *surface,
                     double sx, double sy)
{
	struct tw_pointer *pointer = &grab->seat->pointer;

	tw_pointer_set_focus(pointer, surface, sx, sy);
}

static void
notify_pointer_motion(struct tw_seat_pointer_grab *grab,
                      uint32_t time_msec,
                      double sx, double sy)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	if (client)
		wl_resource_for_each(resource, &client->pointers)
			wl_pointer_send_motion(resource, time_msec,
			                       wl_fixed_from_double(sx),
			                       wl_fixed_from_double(sy));
}

static void
notify_pointer_button(struct tw_seat_pointer_grab *grab,
                      uint32_t time_msec, uint32_t button,
                      enum wl_pointer_button_state state)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	uint32_t serial = wl_display_next_serial(grab->seat->display);
	if (client) {
		wl_resource_for_each(resource, &client->pointers)
			wl_pointer_send_button(resource, serial, time_msec,
			                       button, state);
		//XXX forcusing on the clients should be compositor logic
	}
	grab->seat->last_pointer_serial = serial;
}

static void
notify_pointer_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                   enum wl_pointer_axis orientation, double value,
                   int32_t value_discrete,
                   enum wl_pointer_axis_source source)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	uint32_t version;
	if (client) {
		wl_resource_for_each(resource, &client->pointers) {
			version = wl_resource_get_version(resource);
			if (value)
				wl_pointer_send_axis(resource, time_msec,
				                     orientation,
				                     wl_fixed_from_double(
					                     value));
			else if (value_discrete &&
			         version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
				wl_pointer_send_axis_discrete(resource,
				                              time_msec,
				                              value_discrete);
			if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
				wl_pointer_send_axis_source(resource, source);
		}
		//TODO, we are not able to send stop event?
	}
}

static void
notify_pointer_frame(struct tw_seat_pointer_grab *grab)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	if (client) {
		wl_resource_for_each(resource, &client->pointers)
			if (wl_resource_get_version(resource) >=
			    WL_POINTER_FRAME_SINCE_VERSION)
				wl_pointer_send_frame(resource);
	}
}

static void
notify_pointer_cancel(struct tw_seat_pointer_grab *grab)
{
}


static const struct tw_pointer_grab_interface default_grab_impl = {
	.enter = notify_pointer_enter,
	.motion = notify_pointer_motion,
	.button = notify_pointer_button,
	.axis = notify_pointer_axis,
	.frame = notify_pointer_frame,
	.cancel = notify_pointer_cancel,
};

static void
notify_focused_disappear(struct wl_listener *listener, void *data)
{
	struct tw_pointer *pointer =
		container_of(listener, struct tw_pointer,
		             focused_destroy);
	pointer->focused_surface = NULL;
	pointer->focused_client = NULL;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
}

struct tw_pointer *
tw_seat_new_pointer(struct tw_seat *seat)
{
	struct tw_pointer *pointer = &seat->pointer;
	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER)
		return pointer;

	pointer->btn_count = 0;
	pointer->focused_client = NULL;
	pointer->focused_surface = NULL;
	pointer->default_grab.data = NULL;
	pointer->default_grab.seat = seat;
	pointer->default_grab.impl = &default_grab_impl;
	pointer->grab = &pointer->default_grab;

	wl_list_init(&pointer->focused_destroy.link);
	pointer->focused_destroy.notify = notify_focused_disappear;

	seat->capabilities |= WL_SEAT_CAPABILITY_POINTER;
	tw_seat_send_capabilities(seat);
	return pointer;
}

void
tw_seat_remove_pointer(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	struct wl_resource *resource, *next;
	struct tw_pointer *pointer = &seat->pointer;

	seat->capabilities &= ~WL_SEAT_CAPABILITY_KEYBOARD;
	tw_seat_send_capabilities(seat);
	wl_list_for_each(client, &seat->clients, link)
		wl_resource_for_each_safe(resource, next, &client->pointers)
			wl_resource_destroy(resource);

	pointer->grab = &pointer->default_grab;
	pointer->focused_client = NULL;
	pointer->focused_surface = NULL;
	tw_reset_wl_list(&pointer->focused_destroy.link);
}

void
tw_pointer_start_grab(struct tw_pointer *pointer,
                      struct tw_seat_pointer_grab *grab)
{
	struct tw_seat *seat =
		container_of(pointer, struct tw_seat, pointer);
	pointer->grab = grab;
	grab->seat = seat;
}

void
tw_pointer_end_grab(struct tw_pointer *pointer)
{
	if (pointer->grab && pointer->grab != &pointer->default_grab &&
	    pointer->grab->impl->cancel)
		pointer->grab->impl->cancel(pointer->grab);
	pointer->grab = &pointer->default_grab;
}

void
tw_pointer_set_focus(struct tw_pointer *pointer,
                     struct wl_resource *wl_surface,
                     double sx, double sy)
{
	uint32_t serial;
	struct wl_resource *res;
	struct tw_seat_client *client;
	struct tw_seat *seat = container_of(pointer, struct tw_seat, pointer);

	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client && !wl_list_empty(&client->pointers) ) {
		tw_pointer_clear_focus(pointer);

		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(res, &client->pointers)
			wl_pointer_send_enter(res, serial, wl_surface,
			                      wl_fixed_from_double(sx),
			                      wl_fixed_from_double(sy));
		pointer->focused_client = client;
		pointer->focused_surface = wl_surface;

		tw_reset_wl_list(&pointer->focused_destroy.link);
		wl_resource_add_destroy_listener(wl_surface,
		                                 &pointer->focused_destroy);
	}
}

void
tw_pointer_clear_focus(struct tw_pointer *pointer)
{
	struct tw_seat_client *client;
	struct wl_resource *res;
	uint32_t serial;
	struct tw_seat *seat = container_of(pointer, struct tw_seat, pointer);

	if (pointer->focused_surface && pointer->focused_client) {
		client = pointer->focused_client;
		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(res, &client->pointers)
			wl_pointer_send_leave(res, serial,
			                      pointer->focused_surface);
	}
	pointer->focused_client = NULL;
	pointer->focused_surface = NULL;
}

void
tw_pointer_notify_enter(struct tw_pointer *pointer,
                        struct wl_resource *wl_surface,
                        double sx, double sy)
{
	if (pointer->grab && pointer->grab->impl->enter)
		pointer->grab->impl->enter(pointer->grab, wl_surface, sx, sy);
}

void
tw_pointer_notify_motion(struct tw_pointer *pointer, uint32_t time_msec,
                         double sx, double sy)
{
	if (pointer->grab && pointer->grab->impl->motion)
		pointer->grab->impl->motion(pointer->grab, time_msec, sx, sy);
}

void
tw_pointer_notify_button(struct tw_pointer *pointer, uint32_t time_msec,
                         uint32_t button, enum wl_pointer_button_state state)
{
	if (pointer->grab && pointer->grab->impl->button)
		pointer->grab->impl->button(pointer->grab, time_msec,
		                                   button, state);
}

void
tw_pointer_notify_axis(struct tw_pointer *pointer, uint32_t time_msec,
                       enum wl_pointer_axis axis, double val, int val_disc,
                       enum wl_pointer_axis_source source)
{
	if (pointer->grab && pointer->grab->impl->axis)
		pointer->grab->impl->axis(pointer->grab, time_msec, axis,
		                          val, val_disc, source);
}

void
tw_pointer_notify_frame(struct tw_pointer *pointer)
{
	if (pointer->grab && pointer->grab->impl->frame)
		pointer->grab->impl->frame(pointer->grab);
}
