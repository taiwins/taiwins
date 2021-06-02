/*
 * popup_grab.c - taiwins server common interface for popup windows
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
#include <wayland-util.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/popup_grab.h>

static const struct tw_pointer_grab_interface popup_pointer_grab_impl;
static const struct tw_touch_grab_interface popup_touch_grab_impl;

static void tw_popup_grab_close(struct tw_popup_grab *grab);

static void
popup_pointer_grab_button(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec, uint32_t button,
                          enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct wl_resource *wl_surface = grab->data;
	bool on_popup = (wl_surface == pointer->focused_surface);
	struct tw_popup_grab *popup_grab =
		wl_container_of(grab, popup_grab, pointer_grab);
	pointer->default_grab.impl->button(&pointer->default_grab,
	                                   time_msec, button, state);
	//TODO it is either we have pointer to be able to focus on popup before
	//this or defer the grab after the release button.
	if (!on_popup)
		tw_popup_grab_close(popup_grab);
}

static const struct tw_pointer_grab_interface popup_pointer_grab_impl = {
	.enter = tw_pointer_default_enter,
	.motion = tw_pointer_default_motion,
	.button = popup_pointer_grab_button,
	.axis = tw_pointer_default_axis,
	.frame = tw_pointer_default_frame,
	.cancel = tw_pointer_default_cancel,
};


static void
popup_touch_grab_down(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                      uint32_t touch_id, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	struct wl_resource *wl_surface = grab->data;
	struct tw_popup_grab *popup_grab =
		wl_container_of(grab, popup_grab, touch_grab);
	bool on_popup = (wl_surface == touch->focused_surface);

	touch->default_grab.impl->down(&touch->default_grab,
	                               time_msec, touch_id,
	                               sx, sy);
	if (!on_popup)
		tw_popup_grab_close(popup_grab);
}

static const struct tw_touch_grab_interface popup_touch_grab_impl = {
	.down = popup_touch_grab_down,
	.up = tw_touch_default_up,
	.motion = tw_touch_default_motion,
	.enter = tw_touch_default_enter,
	.touch_cancel = tw_touch_default_touch_cancel,
	.cancel = tw_touch_default_cancel,
};

void
tw_popup_grab_close(struct tw_popup_grab *grab)
{
	struct tw_seat *seat = grab->pointer_grab.seat;
	struct tw_pointer *pointer = &seat->pointer;
	struct tw_touch *touch = &seat->touch;

	//we need to end grab unconditionally since we could be nested.
	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER)
		tw_pointer_end_grab(pointer, &grab->pointer_grab);
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)
		tw_touch_end_grab(touch, &grab->touch_grab);
	wl_signal_emit(&grab->close, grab);

	tw_reset_wl_list(&grab->resource_destroy.link);
}

static void
notify_resource_destroy(struct wl_listener *listener, void *userdata)
{
	struct tw_popup_grab *grab =
		wl_container_of(listener, grab, resource_destroy);
	tw_popup_grab_close(grab);
}

WL_EXPORT void
tw_popup_grab_start(struct tw_popup_grab *grab, struct tw_seat *seat)
{
	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) {
		tw_pointer_start_grab(&seat->pointer, &grab->pointer_grab,
		                      TW_POPUP_GRAB_ORDER);
		//TODO: this is a hack, should work most of the time
		tw_pointer_notify_enter(&seat->pointer, grab->focus, 0, 0);
	}
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		tw_touch_start_grab(&seat->touch, &grab->touch_grab,
		                    TW_POPUP_GRAB_ORDER);
		tw_touch_notify_enter(&seat->touch, grab->focus, 0, 0);
	}
}

WL_EXPORT void
tw_popup_grab_init(struct tw_popup_grab *grab, struct tw_surface *surface,
                   struct wl_resource *obj)
{
	struct wl_resource *wl_surface = surface->resource;
	grab->pointer_grab.data = wl_surface;
	grab->pointer_grab.impl = &popup_pointer_grab_impl;

	grab->touch_grab.data = wl_surface;
	grab->touch_grab.impl = &popup_touch_grab_impl;

	grab->focus = wl_surface;
	grab->interface = obj;
	tw_set_resource_destroy_listener(obj,
	                                 &grab->resource_destroy,
	                                 notify_resource_destroy);
	wl_signal_init(&grab->close);
}
