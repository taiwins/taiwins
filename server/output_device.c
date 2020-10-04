/*
 * output_device.c - taiwins server output device implementation
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

#include <string.h>
#include <pixman.h>
#include <wayland-server.h>
#include <taiwins/objects/matrix.h>
#include <taiwins/objects/logger.h>

#include "output_device.h"

static enum wl_output_transform
inverse_wl_transform(enum wl_output_transform t)
{
	if ((t & WL_OUTPUT_TRANSFORM_90) &&
	    !(t & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		t ^= WL_OUTPUT_TRANSFORM_180;
	}
	return t;
}

static void
output_get_effective_resolution(const struct tw_output_device_state *state,
                                int *width, int *height)
{
	if (state->transform % WL_OUTPUT_TRANSFORM_180 == 0) {
		*width = state->current_mode.w;
		*height = state->current_mode.h;
	} else {
		*width = state->current_mode.h;
		*height = state->current_mode.w;
	}
	*width /= state->scale;
	*height /= state->scale;
}

static void
output_device_state_init(struct tw_output_device_state *state,
                         struct tw_output_device *device)
{
	state->enabled = true;
	state->scale = 1.0;
	state->current_mode.w = 0;
	state->current_mode.h = 0;
	state->current_mode.refresh = 0;
	state->current_mode.preferred = false;
	state->subpixel = WL_OUTPUT_SUBPIXEL_NONE;
	state->transform = WL_OUTPUT_TRANSFORM_NORMAL;

        state->gx = 0;
	state->gy = 0;
	tw_mat3_init(&state->view_2d);
}

void
tw_output_device_init(struct tw_output_device *device,
                      const struct tw_output_device_impl *impl)
{
	device->phys_width = 0;
	device->phys_height = 0;
	device->impl = impl;
	wl_array_init(&device->available_modes);
	wl_list_init(&device->link);

	output_device_state_init(&device->state, device);
	output_device_state_init(&device->pending, device);

	wl_signal_init(&device->events.destroy);
	wl_signal_init(&device->events.info);
	wl_signal_init(&device->events.new_frame);
	wl_signal_init(&device->events.info);
	wl_signal_init(&device->events.commit_state);
}

void
tw_output_device_fini(struct tw_output_device *device)
{
	wl_signal_emit(&device->events.destroy, device);

	wl_array_release(&device->available_modes);
	wl_list_remove(&device->link);
}

void
tw_output_device_set_scale(struct tw_output_device *device, float scale)
{
	if (scale <= 0) {
		tw_logl_level(TW_LOG_WARN, "attempt to set invalid scale %f"
		              " on output device", scale);
		return;
	}
	device->pending.scale = scale;
}

void
tw_output_device_commit_state(struct tw_output_device *device)
{
	//emit for backend
	device->impl->commit_state(device);
	wl_signal_emit(&device->events.commit_state, device);
	//emit for sending new backend info.
	wl_signal_emit(&device->events.info, device);
}

pixman_rectangle32_t
tw_output_device_geometry(const struct tw_output_device *output)
{
	int width, height;

	output_get_effective_resolution(&output->state, &width, &height);
	return (pixman_rectangle32_t){
		output->state.gx, output->state.gy,
		width, height
	};
}

void
tw_output_device_loc_to_global(const struct tw_output_device *output,
                               float x, float y, float *gx, float *gy)
{
	int width, height;

	output_get_effective_resolution(&output->state, &width, &height);

	*gx = output->state.gx + x * width;
	*gy = output->state.gy + y * height;
}

void
tw_output_device_state_rebuild_view_mat(struct tw_output_device_state *state)
{
	struct tw_mat3 glproj, tmp;
	int width, height;

	//the transform should be
	// T' = glproj * inv_wl_transform * scale * -translate * T

	//effective resolution is going from
	output_get_effective_resolution(state, &width, &height);

	//output scale and inverse transform.
	tw_mat3_translate(&state->view_2d, -state->gx, -state->gy);
	tw_mat3_transform_rect(&tmp, false,
	                       inverse_wl_transform(state->transform),
	                       width, height, state->scale);
	//glproj matrix,
	tw_mat3_init(&glproj);
	glproj.d[4] = -1;
	glproj.d[7] = state->current_mode.h;

	tw_mat3_multiply(&state->view_2d, &tmp, &state->view_2d);
	tw_mat3_multiply(&state->view_2d, &glproj, &state->view_2d);
}
