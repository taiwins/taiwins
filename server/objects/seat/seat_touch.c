/*
 * seat_pointer.c - taiwins server wl_touch implemetation
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
notify_touch_enter(struct tw_seat_touch_grab *grab,
                   struct wl_resource *surface, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	tw_touch_set_focus(touch, surface, wl_fixed_from_double(sx),
	                   wl_fixed_from_double(sy));
}

static void
notify_touch_down(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                  uint32_t touch_id, double sx, double sy)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;
	uint32_t serial =  wl_display_get_serial(grab->seat->display);
	if (touch->focused_client)
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_down(touch_res, serial, time_msec,
			                   touch->focused_surface,
			                   touch_id,
			                   wl_fixed_from_double(sx),
			                   wl_fixed_from_double(sy));
			wl_touch_send_frame(touch_res);
		}
	grab->seat->last_touch_serial = serial;
}

static void
notify_touch_up(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                uint32_t touch_id)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;
	uint32_t serial =  wl_display_get_serial(grab->seat->display);

	if (touch->focused_client)
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_up(touch_res, serial, time_msec,
			                 touch_id);
			wl_touch_send_frame(touch_res);
		}

}

static void
notify_touch_motion(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                    uint32_t touch_id, double sx, double sy)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;

	if (touch->focused_client) {
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_motion(touch_res, time_msec,
			                     touch_id,
			                     wl_fixed_from_double(sx),
			                     wl_fixed_from_double(sy));
			wl_touch_send_frame(touch_res);
		}
	}

}

static void
notify_touch_cancel_event(struct tw_seat_touch_grab *grab)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;

	if (touch->focused_client)
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches)
			wl_touch_send_cancel(touch_res);
}

static void
notify_touch_cancel(struct tw_seat_touch_grab *grab)
{
}

static const struct tw_touch_grab_interface default_grab_impl = {
	.enter = notify_touch_enter,
	.down = notify_touch_down,
	.up = notify_touch_up,
	.motion = notify_touch_motion,
	.touch_cancel = notify_touch_cancel_event,
	.cancel = notify_touch_cancel,
};

static void
notify_focused_disappear(struct wl_listener *listener, void *data)
{
	struct tw_touch *touch =
		container_of(listener, struct tw_touch, focused_destroy);
	touch->focused_surface = NULL;
	touch->focused_client = NULL;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
}

struct tw_touch *
tw_seat_new_touch(struct tw_seat *seat)
{
	struct tw_touch *touch = &seat->touch;
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)
		return touch;
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
	touch->default_grab.data = NULL;
	touch->default_grab.impl = &default_grab_impl;
	touch->default_grab.seat = seat;
	touch->grab = &touch->default_grab;

	wl_list_init(&touch->focused_destroy.link);
	touch->focused_destroy.notify = notify_focused_disappear;

	seat->capabilities |= WL_SEAT_CAPABILITY_TOUCH;
	tw_seat_send_capabilities(seat);
	return touch;
}

void
tw_seat_remove_touch(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	struct wl_resource *resource, *next;
	struct tw_touch *touch = &seat->touch;

	seat->capabilities &= ~WL_SEAT_CAPABILITY_KEYBOARD;
	tw_seat_send_capabilities(seat);
	wl_list_for_each(client, &seat->clients, link)
		wl_resource_for_each_safe(resource, next, &client->touches)
			wl_resource_destroy(resource);

	touch->grab = &touch->default_grab;
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
}

void
tw_touch_start_grab(struct tw_touch *touch, struct tw_seat_touch_grab *grab)
{
	struct tw_seat *seat =
		container_of(touch, struct tw_seat, touch);
	touch->grab = grab;
	grab->seat = seat;
}

void
tw_touch_end_grab(struct tw_touch *touch)
{
	if (touch->grab && touch->grab != &touch->default_grab &&
	    touch->grab->impl->cancel)
		touch->grab->impl->cancel(touch->grab);
	touch->grab = &touch->default_grab;
}

void
tw_touch_set_focus(struct tw_touch *touch,
                     struct wl_resource *wl_surface,
                     double sx, double sy)
{
	struct tw_seat_client *client;
	struct tw_seat *seat = container_of(touch, struct tw_seat, touch);

	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client && !wl_list_empty(&client->touches)) {
		tw_touch_clear_focus(touch);

		touch->focused_client = client;
		touch->focused_surface = wl_surface;

		tw_reset_wl_list(&touch->focused_destroy.link);
		wl_resource_add_destroy_listener(wl_surface,
		                                 &touch->focused_destroy);
	}
}

void
tw_touch_clear_focus(struct tw_touch *touch)
{
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
}

void
tw_touch_notify_down(struct tw_touch *touch, uint32_t time_msec, uint32_t id,
                     double sx, double sy)
{
	if (touch->grab && touch->grab->impl->down)
		touch->grab->impl->down(touch->grab, time_msec, id,
		                        sx, sy);
}

void
tw_touch_notify_up(struct tw_touch *touch, uint32_t time_msec,
                   uint32_t touch_id)
{
	if (touch->grab && touch->grab->impl->up)
		touch->grab->impl->up(touch->grab, time_msec, touch_id);
}

void
tw_touch_notify_motion(struct tw_touch *touch, uint32_t time_msec,
                       uint32_t touch_id, double sx, double sy)
{
	if (touch->grab && touch->grab->impl->motion)
		touch->grab->impl->motion(touch->grab, time_msec, touch_id,
		                          sx, sy);
}

void
tw_touch_notify_enter(struct tw_touch *touch,
                      struct wl_resource *surface, double sx, double sy)
{
	if (touch->grab && touch->grab->impl->enter)
		touch->grab->impl->enter(touch->grab, surface, sx, sy);
}

void
tw_touch_notify_cancel(struct tw_touch *touch)
{
	if (touch->grab && touch->grab->impl->touch_cancel)
		touch->grab->impl->touch_cancel(touch->grab);
}
