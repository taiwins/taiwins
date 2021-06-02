/*
 * popup_grab.h - taiwins server common interface for popup windows
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

#ifndef TW_POPUP_GRAB_H
#define TW_POPUP_GRAB_H

#include <wayland-server.h>

#include "seat.h"
#include "surface.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_POPUP_GRAB_ORDER	0x100

/**
 * @brief common interface used for popup windows
 *
 * popup is a subsurface with a specific grab, possibly nested. It is used in
 * wl_shell, xdg_shell, xsurface, wlr_layer_shell and possibly taiwins_shell as
 * well.
 */
struct tw_popup_grab {
	struct tw_seat_pointer_grab pointer_grab;
	struct tw_seat_touch_grab touch_grab;
	struct wl_resource *focus, *interface;
	struct tw_seat *seat;
	struct wl_listener resource_destroy;

	struct wl_signal close;
};

void
tw_popup_grab_init(struct tw_popup_grab *grab, struct tw_surface *surface,
                   struct wl_resource *obj);

void
tw_popup_grab_start(struct tw_popup_grab *grab, struct tw_seat *seat);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
