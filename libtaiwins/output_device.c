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

#include <time.h>
#include <string.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/matrix.h>
#include <taiwins/objects/logger.h>

#include <taiwins/output_device.h>

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
	state->transform = WL_OUTPUT_TRANSFORM_NORMAL;

        state->gx = 0;
	state->gy = 0;
}

void
tw_output_device_init(struct tw_output_device *device,
                      const struct tw_output_device_impl *impl)
{
	device->phys_width = 0;
	device->phys_height = 0;
	device->impl = impl;
	device->subpixel = WL_OUTPUT_SUBPIXEL_NONE;
	wl_array_init(&device->available_modes);
	wl_list_init(&device->link);

	output_device_state_init(&device->state, device);
	output_device_state_init(&device->pending, device);

	wl_signal_init(&device->events.destroy);
	wl_signal_init(&device->events.info);
	wl_signal_init(&device->events.new_frame);
	wl_signal_init(&device->events.info);
	wl_signal_init(&device->events.present);
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
tw_output_device_set_id(struct tw_output_device *device, int id)
{
	device->id = id;
}

void
tw_output_device_set_pos(struct tw_output_device *device, int gx, int gy)
{
	device->pending.gx = gx;
	device->pending.gy = gy;
}

void
tw_output_device_set_custom_mode(struct tw_output_device *device,
                                 unsigned width, unsigned height, int refresh)
{
	device->pending.current_mode.w = width;
	device->pending.current_mode.h = height;
	device->pending.current_mode.preferred = false;
	device->pending.current_mode.refresh = refresh;
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
tw_output_device_set_transform(struct tw_output_device *device,
                               enum wl_output_transform transform)
{
	device->pending.transform = transform;
}

void
tw_output_device_enable(struct tw_output_device *device, bool enable)
{
	device->pending.enabled = enable;
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

void
tw_output_device_present(struct tw_output_device *device,
                         struct tw_event_output_device_present *event)
{
	struct tw_event_output_device_present _event = {
		.device = device,
	};
	struct timespec now;
	if (event == NULL) {
		event = &_event;
		clock_gettime(CLOCK_MONOTONIC, &now);
		event->time = now;
	}
	wl_signal_emit(&device->events.present, event);
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
tw_output_device_raw_resolution(const struct tw_output_device *device,
                                unsigned *width, unsigned *height)
{
	*width = device->state.current_mode.w;
	*height = device->state.current_mode.h;
}
