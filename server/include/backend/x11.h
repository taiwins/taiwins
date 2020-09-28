/*
 * x11.h - taiwins server x11 backend header
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

#ifndef TW_X11_BACKEND_H
#define TW_X11_BACKEND_H

#include "backend.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend_impl *
tw_x11_backend_create(struct wl_display *display, const char *x11_display);

bool
tw_x11_backend_add_output(struct tw_backend *backend,
                          unsigned int width, unsigned int height);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
