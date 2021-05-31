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

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <wayland-util.h>

static void
clear_focus_no_signal(struct tw_pointer *pointer)
{
	struct tw_seat_client *client;
	struct wl_resource *res;
	uint32_t serial;
	struct tw_seat *seat = wl_container_of(pointer, seat, pointer);

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

static void
tw_pointer_set_focus(struct tw_pointer *pointer,
                     struct wl_resource *wl_surface,
                     double sx, double sy)
{
	uint32_t serial;
	struct wl_resource *res;
	struct tw_seat_client *client;
	struct tw_seat *seat = wl_container_of(pointer, seat, pointer);

	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client && !wl_list_empty(&client->pointers) ) {
		clear_focus_no_signal(pointer);

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
		wl_signal_emit(&seat->signals.focus, pointer);
	}
}

static void
tw_pointer_clear_focus(struct tw_pointer *pointer)
{
	struct tw_seat *seat = wl_container_of(pointer, seat, pointer);

        clear_focus_no_signal(pointer);
        wl_signal_emit(&seat->signals.unfocus, pointer);
}

WL_EXPORT void
tw_pointer_default_enter(struct tw_seat_pointer_grab *grab,
                         struct wl_resource *surface,
                         double sx, double sy)
{
	struct tw_pointer *pointer = &grab->seat->pointer;

	if (surface)
		tw_pointer_set_focus(pointer, surface, sx, sy);
	else
		tw_pointer_clear_focus(pointer);
}

WL_EXPORT void
tw_pointer_default_motion(struct tw_seat_pointer_grab *grab,
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

WL_EXPORT void
tw_pointer_default_button(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec, uint32_t button,
                          enum wl_pointer_button_state state)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	uint32_t serial;
	if (client) {
		serial = wl_display_next_serial(grab->seat->display);
		wl_resource_for_each(resource, &client->pointers)
			wl_pointer_send_button(resource, serial, time_msec,
			                       button, state);
		grab->seat->last_pointer_serial = serial;
	}
}

WL_EXPORT void
tw_pointer_default_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                        enum wl_pointer_axis orientation, double val,
                        int32_t value_discrete,
                        enum wl_pointer_axis_source source)
{
	struct wl_resource *resource;
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct tw_seat_client *client = pointer->focused_client;
	const uint32_t ver_discrete = WL_POINTER_AXIS_DISCRETE_SINCE_VERSION;
	const uint32_t ver_source = WL_POINTER_AXIS_SOURCE_SINCE_VERSION;

	uint32_t ver;

	if (client) {
		wl_resource_for_each(resource, &client->pointers) {
			ver = wl_resource_get_version(resource);
			if (ver >= ver_source)
				wl_pointer_send_axis_source(resource, source);
			if (val) {
				if (value_discrete && ver >= ver_discrete)
					wl_pointer_send_axis(resource, time_msec,
					                     orientation,
					                     value_discrete);
				wl_pointer_send_axis(resource, time_msec,
				                     orientation,
				                     wl_fixed_from_double(val));
			} else if (ver >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
				wl_pointer_send_axis_stop(resource, time_msec,
				                          orientation);
		}
	}
}

WL_EXPORT void
tw_pointer_default_frame(struct tw_seat_pointer_grab *grab)
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

WL_EXPORT void
tw_pointer_default_cancel(struct tw_seat_pointer_grab *grab)
{
}

static const struct tw_pointer_grab_interface default_grab_impl = {
	.enter = tw_pointer_default_enter,
	.motion = tw_pointer_default_motion,
	.button = tw_pointer_default_button,
	.axis = tw_pointer_default_axis,
	.frame = tw_pointer_default_frame,
	.cancel = tw_pointer_default_cancel,
};

static void
notify_focused_disappear(struct wl_listener *listener, void *data)
{
	struct tw_pointer *pointer =
		wl_container_of(listener, pointer, focused_destroy);

        pointer->focused_surface = NULL;
	pointer->focused_client = NULL;
	tw_reset_wl_list(&listener->link);
	tw_pointer_clear_focus(pointer); //just for emitting unfocus signal
}

WL_EXPORT struct tw_pointer *
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

	wl_list_init(&pointer->grabs);
	wl_list_init(&pointer->focused_destroy.link);
	pointer->focused_destroy.notify = notify_focused_disappear;

	seat->capabilities |= WL_SEAT_CAPABILITY_POINTER;
	tw_seat_send_capabilities(seat);
	return pointer;
}

WL_EXPORT void
tw_seat_remove_pointer(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	struct wl_resource *resource, *next;
	struct tw_pointer *pointer = &seat->pointer;

	seat->capabilities &= ~WL_SEAT_CAPABILITY_POINTER;
	tw_seat_send_capabilities(seat);

	//now we remove the link of the resources, the resource itself would get
	//destroyed in release request.
	wl_list_for_each(client, &seat->clients, link)
		wl_resource_for_each_safe(resource, next, &client->pointers)
			tw_reset_wl_list(wl_resource_get_link(resource));

	pointer->grab = &pointer->default_grab;
	pointer->focused_client = NULL;
	pointer->focused_surface = NULL;
	tw_reset_wl_list(&pointer->focused_destroy.link);
}

WL_EXPORT void
tw_pointer_start_grab(struct tw_pointer *pointer,
                      struct tw_seat_pointer_grab *grab)
{
	struct tw_seat *seat = wl_container_of(pointer, seat, pointer);

	if (pointer->grab != grab &&
	    !tw_find_list_elem(&pointer->grabs, &grab->node.link)) {
		pointer->grab = grab;
		grab->seat = seat;
		wl_list_insert(&pointer->grabs, &grab->node.link);
	}

}

WL_EXPORT void
tw_pointer_end_grab(struct tw_pointer *pointer,
                    struct tw_seat_pointer_grab *grab)
{
	struct tw_seat *seat = wl_container_of(pointer, seat, pointer);
	struct tw_seat_pointer_grab *old = pointer->grab;

	if (tw_find_list_elem(&pointer->grabs, &grab->node.link)) {
		if (grab->impl->cancel)
			grab->impl->cancel(grab);
		tw_reset_wl_list(&grab->node.link);
	}
	//finding previous grab from list or default if stack is empty
	if (!wl_list_empty(&pointer->grabs))
		grab = wl_container_of(pointer->grabs.next, grab, node.link);
	else
		grab = &pointer->default_grab;
	pointer->grab = grab;
	pointer->grab->seat = seat;
	if (grab != old && grab->impl->restart)
		grab->impl->restart(grab);
}
