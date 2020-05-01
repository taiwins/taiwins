/*
 * bus.h - taiwins server compositor header
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

#ifndef TW_COMPOSITOR_H
#define TW_COMPOSITOR_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <libweston/libweston.h>

#include "taiwins.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_bus;
struct tw_backend;
struct tw_xwayland;
struct tw_theme;

struct tw_bus *
tw_setup_bus(struct weston_compositor *ec);

struct tw_xwayland *
tw_setup_xwayland(struct weston_compositor *ec);

void
tw_xwayland_enable(struct tw_xwayland *xwayland, bool enable);

struct tw_theme *
tw_setup_theme(struct weston_compositor *ec);

void
tw_theme_notify(struct tw_theme *theme);


#ifdef  __cplusplus
}
#endif



#endif /* EOF */
