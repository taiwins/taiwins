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
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <ctypes/helpers.h>

#include "seat.h"
#include "taiwins.h"

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

static uint32_t
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
			}
	return serial;
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
	if (client) {
		wl_resource_for_each(resource, &client->pointers) {
			if (value)
				wl_pointer_send_axis(resource, time_msec,
				                     orientation,
				                     wl_fixed_from_double(
					                     value));
			else if (value_discrete)
				wl_pointer_send_axis_discrete(resource,
				                              time_msec,
				                              value_discrete);
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
	if (pointer->grab != &pointer->default_grab &&
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

	tw_pointer_clear_focus(pointer);
	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client) {
		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(res, &client->pointers)
			wl_pointer_send_enter(res, serial, wl_surface,
			                      wl_fixed_from_double(sx),
			                      wl_fixed_from_double(sy));
		pointer->focused_client = client;
		pointer->focused_surface = wl_surface;
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
tw_pointer_noop_enter(struct tw_seat_pointer_grab *grab,
                      struct wl_resource *surface, double sx, double sy) {}
void
tw_pointer_noop_motion(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                       double sx, double sy) {}
uint32_t
tw_pointer_noop_button(struct tw_seat_pointer_grab *grab,
                       uint32_t time_msec, uint32_t button,
                       enum wl_pointer_button_state state)
{
	return 0;
}

void
tw_pointer_noop_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                     enum wl_pointer_axis orientation, double value,
                     int32_t value_discrete,
                     enum wl_pointer_axis_source source) {}
void
tw_pointer_noop_frame(struct tw_seat_pointer_grab *grab) {}

void
tw_pointer_noop_cancel(struct tw_seat_pointer_grab *grab) {}
