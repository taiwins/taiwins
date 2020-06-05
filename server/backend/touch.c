/*
 * pointer.c - taiwins backend touch functions
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
notify_backend_touch_remove(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.destroy);
	wl_list_remove(&seat->touch.destroy.link);
	//update the capabilities
	seat->capabilities &= ~TW_INPUT_CAP_TOUCH;
	tw_seat_remove_touch(seat->tw_seat);
	//update signals
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);
	if (seat->capabilities == 0)
		tw_backend_seat_destroy(seat);
}

static void
notify_backend_touch_down(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.down);
	struct wlr_event_touch_down *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_up(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.up);
	struct wlr_event_touch_up *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_motion(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.motion);
	struct wlr_event_touch_motion *event = data;
	(void)seat; (void)event;
}

static void
notify_backend_touch_cancel(struct wl_listener *listener, void *data)
{
	struct tw_backend_seat *seat =
		container_of(listener, struct tw_backend_seat,
		             touch.cancel);
	struct wlr_event_touch_cancel *event = data;
	(void)seat; (void)event;
}

void
tw_backend_new_touch(struct tw_backend *backend,
                     struct wlr_input_device *dev)
{
	struct tw_backend_seat *seat =
		tw_backend_seat_find_create(backend, dev,
		                            TW_INPUT_CAP_TOUCH);
	if (!seat) return;

	seat->touch.device = dev;
	//update capabilities
	seat->capabilities |= TW_INPUT_CAP_TOUCH;
	tw_seat_new_touch(seat->tw_seat);
	wl_signal_emit(&seat->backend->seat_ch_signal, seat);

	//install listeners
	wl_list_init(&seat->touch.destroy.link);
	seat->touch.destroy.notify = notify_backend_touch_remove;
	wl_signal_add(&dev->events.destroy, &seat->touch.destroy);

	wl_list_init(&seat->touch.down.link);
	seat->touch.down.notify = notify_backend_touch_down;
	wl_signal_add(&dev->touch->events.down, &seat->touch.down);

	wl_list_init(&seat->touch.up.link);
	seat->touch.up.notify = notify_backend_touch_up;
	wl_signal_add(&dev->touch->events.up, &seat->touch.up);

	wl_list_init(&seat->touch.motion.link);
	seat->touch.motion.notify = notify_backend_touch_motion;
	wl_signal_add(&dev->touch->events.motion, &seat->touch.motion);

	wl_list_init(&seat->touch.cancel.link);
	seat->touch.cancel.notify = notify_backend_touch_cancel;
	wl_signal_add(&dev->touch->events.cancel, &seat->touch.cancel);

}
