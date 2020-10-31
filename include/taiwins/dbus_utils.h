/*
 * dbus_utils.h - taiwins server dbus utils
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

#ifndef TW_DBUS_UTILS_H
#define TW_DBUS_UTILS_H

#include <unistd.h>
#include <sys/eventfd.h>
#include <tdbus.h>
#include <wayland-server-core.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct wl_event_source *
tw_bind_tdbus_for_wl_display(struct tdbus *bus, struct wl_display *display);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
