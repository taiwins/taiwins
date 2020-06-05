/*
 * pointer.c - taiwins backend pointer functions
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

#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <ctypes/helpers.h>

#include <seat/seat.h>

#include "backend.h"
#include "backend_internal.h"

static void
notify_backend_pointer_button(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.button);
	struct wlr_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;
	uint32_t state = event->state == WLR_BUTTON_PRESSED ?
		WL_POINTER_BUTTON_STATE_PRESSED :
		WL_POINTER_BUTTON_STATE_RELEASED;

	if (seat_pointer->grab->impl->button)
		seat_pointer->grab->impl->button(seat_pointer->grab,
		                                 event->time_msec,
		                                 event->button,
		                                 state);
}

static void
notify_backend_pointer_motion(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.motion);
	struct wlr_event_pointer_motion *event = data;
	struct tw_backend *backend = seat->backend;
	struct wlr_cursor *cursor = backend->global_cursor;

	//we have only relative motion now.
        cursor->x += event->delta_x;
        cursor->y += event->delta_y;

        //obviously we do not have any information about surface at this point.
        wl_signal_emit(&cursor->events.motion, data);
}

static void
notify_backend_pointer_axis(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.axis);
	struct wlr_event_pointer_axis *event = data;
	struct tw_backend *backend = seat->backend;
	struct wlr_cursor *cursor = backend->global_cursor;

	//TODO: All we can do is forwarding the event for now.
	wl_signal_emit(&cursor->events.axis, event);
}

static void
notify_backend_pointer_frame(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.frame);
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;
	if (seat_pointer->grab->impl->frame)
		seat_pointer->grab->impl->frame(seat_pointer->grab);
}


static void
notify_backend_pointer_remove(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             pointer.destroy);

	wl_list_remove(&seat->pointer.destroy.link);
	//update the capabilities
	seat->capabilities &= ~TW_INPUT_CAP_POINTER;
	tw_seat_remove_pointer(seat->tw_seat);
	//update the signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);
	if (seat->capabilities == 0)
		tw_backend_seat_destroy(seat);
}

void
tw_backend_new_pointer(struct tw_backend *backend,
                       struct wlr_input_device *dev)
{
	struct wlr_pointer *pointer = dev->pointer;
	struct tw_backend_seat *seat =
		tw_backend_seat_find_create(backend, dev,
		                            TW_INPUT_CAP_POINTER);
	if (!seat) return;
	seat->pointer.device = dev;
        //update the capabilities
	seat->capabilities |= TW_INPUT_CAP_POINTER;
	tw_seat_new_pointer(seat->tw_seat);
	//update the signals earlier than listener to have a
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	//add listeners
	wl_list_init(&seat->pointer.destroy.link);
	seat->pointer.destroy.notify = notify_backend_pointer_remove;
	wl_signal_add(&dev->events.destroy, &seat->pointer.destroy);

	wl_list_init(&seat->pointer.button.link);
	seat->pointer.button.notify = notify_backend_pointer_button;
	wl_signal_add(&pointer->events.button, &seat->pointer.button);

	wl_list_init(&seat->pointer.motion.link);
	seat->pointer.motion.notify = notify_backend_pointer_motion;
	wl_signal_add(&pointer->events.motion, &seat->pointer.motion);

	wl_list_init(&seat->pointer.axis.link);
	seat->pointer.axis.notify = notify_backend_pointer_axis;
	wl_signal_add(&pointer->events.axis, &seat->pointer.axis);

        wl_list_init(&seat->pointer.frame.link);
	seat->pointer.frame.notify = notify_backend_pointer_frame;
	wl_signal_add(&pointer->events.frame, &seat->pointer.frame);

}
