/*
 * internal.c - taiwins engine internal header
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

#ifndef TW_ENGINE_INTERNAL_H
#define TW_ENGINE_INTERNAL_H

#include <taiwins/engine.h>
#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_engine_seat *
tw_engine_seat_find_create(struct tw_engine *engine, unsigned int id);

bool
tw_engine_new_output(struct tw_engine *engine,
                     struct tw_output_device *device);
void
tw_engine_new_xdg_output(struct tw_engine *engine,
                         struct wl_resource *resource);
void
tw_engine_seat_add_input_device(struct tw_engine_seat *seat,
                                struct tw_input_device *device);
void
tw_engine_seat_release(struct tw_engine_seat *seat);

struct tw_surface *
tw_engine_pick_surface_from_layers(struct tw_engine *backend,
                                   float x, float y, float *sx,  float *sy);
void
tw_engine_set_render(struct tw_engine *engine, struct tw_render_context *ctx);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
