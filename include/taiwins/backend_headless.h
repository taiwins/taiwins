/*
 * headless.h - taiwins server headless backend header
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

#ifndef TW_HEADLESS_BACKEND_H
#define TW_HEADLESS_BACKEND_H

#include "backend.h"
#include "taiwins/input_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend *
tw_headless_backend_create(struct wl_display *display);

bool
tw_headless_backend_add_output(struct tw_backend *backend,
                               unsigned int width, unsigned int height);
bool
tw_headless_backend_add_input_device(struct tw_backend *backend,
                                     enum tw_input_device_type type);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
