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

#include <libinput.h>
#include <wlr/types/wlr_seat.h>

#include <taiwins/objects/seat.h>
#include "backend.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend_impl {
	struct tw_backend *backend;

	struct wl_listener head_add;
	struct wl_listener input_add;

	struct wl_listener compositor_create_surface;
	struct wl_listener compositor_create_subsurface;
	struct wl_listener compositor_create_region;
	struct wl_listener surface_dirty_output;
	struct wl_listener surface_destroy;
};

struct tw_backend_seat *
tw_backend_seat_find_create(struct tw_backend *backend,
                            struct wlr_input_device *dev,
                            enum tw_input_device_cap cap);
void
tw_backend_seat_destroy(struct tw_backend_seat *seat);

void
tw_backend_new_output(struct tw_backend *backend,
                      struct wlr_output *wlr_output);
void
tw_backend_commit_output_state(struct tw_backend_output *o);

void
tw_backend_new_keyboard(struct tw_backend *backend,
                        struct wlr_input_device *dev);
void
tw_backend_new_pointer(struct tw_backend *backend,
                       struct wlr_input_device *dev);
void
tw_backend_new_touch(struct tw_backend *backend,
                     struct wlr_input_device *dev);
void
tw_backend_init_impl(struct tw_backend_impl *impl,
                     struct tw_backend *backend);
void
tw_backend_fini_impl(struct tw_backend_impl *impl);

bool
tw_backend_valid_libinput_device(struct libinput_device *device);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
