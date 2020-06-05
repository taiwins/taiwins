/*
 * backend_internal.h - taiwins server backend internal header
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

#ifndef TW_BACKEND_INTERNAL_H
#define TW_BACKEND_INTERNAL_H

#include <wlr/types/wlr_seat.h>

#include "seat/seat.h"
#include "backend.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend_seat *
tw_backend_seat_find_create(struct tw_backend *backend,
                            struct wlr_input_device *dev,
                            enum tw_input_device_cap cap);
void
tw_backend_seat_destroy(struct tw_backend_seat *seat);

void
tw_backend_new_keyboard(struct tw_backend *backend,
                        struct wlr_input_device *dev);
void
tw_backend_new_pointer(struct tw_backend *backend,
                       struct wlr_input_device *dev);
void
tw_backend_new_touch(struct tw_backend *backend,
                     struct wlr_input_device *dev);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
