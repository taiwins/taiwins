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

#include <math.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <pixman.h>
#include <wayland-server.h>
#include <taiwins/objects/matrix.h>
#include <taiwins/objects/logger.h>
#include <taiwins/output_device.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>

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

WL_EXPORT void
tw_output_device_init(struct tw_output_device *device,
                      const struct tw_output_device_impl *impl)
{
	device->phys_width = 0;
	device->phys_height = 0;
	device->impl = impl;
	device->subpixel = WL_OUTPUT_SUBPIXEL_NONE;
	device->clk_id = CLOCK_MONOTONIC;
	wl_list_init(&device->mode_list);
	wl_list_init(&device->link);

	output_device_state_init(&device->state, device);
	output_device_state_init(&device->pending, device);

	wl_signal_init(&device->signals.destroy);
	wl_signal_init(&device->signals.info);
	wl_signal_init(&device->signals.new_frame);
	wl_signal_init(&device->signals.info);
	wl_signal_init(&device->signals.present);
	wl_signal_init(&device->signals.commit_state);
}

WL_EXPORT void
tw_output_device_fini(struct tw_output_device *device)
{
	wl_signal_emit(&device->signals.destroy, device);

	wl_list_remove(&device->link);
}

WL_EXPORT void
tw_output_device_set_id(struct tw_output_device *device, int id)
{
	device->id = id;
}

WL_EXPORT void
tw_output_device_set_pos(struct tw_output_device *device, int gx, int gy)
{
	device->pending.gx = gx;
	device->pending.gy = gy;
}

WL_EXPORT void
tw_output_device_set_mode(struct tw_output_device *device,
                          const struct tw_output_device_mode *mode)
{
	device->pending.current_mode.h = mode->h;
	device->pending.current_mode.w = mode->w;
	device->pending.current_mode.refresh = mode->refresh;
	device->pending.current_mode.preferred = mode->preferred;
}

WL_EXPORT void
tw_output_device_set_custom_mode(struct tw_output_device *device,
                                 unsigned width, unsigned height, int refresh)
{
	device->pending.current_mode.w = width;
	device->pending.current_mode.h = height;
	device->pending.current_mode.preferred = false;
	device->pending.current_mode.refresh = refresh;
}

/** used by backends to extract potential matched mode
 *
 * The algorithm would go through the mode_list searching for the closest mode
 * available(unless there is no modes at all). We return on finding exact mode
 * or the closest mode we can get.
 *
 */
WL_EXPORT struct tw_output_device_mode *
tw_output_device_match_mode(struct tw_output_device *device,
                            int w, int h, int r)
{
	uint64_t min_diff = UINT64_MAX;
	struct tw_output_device_mode *matched = NULL, *mode;

	//return preferred if invalid
	if (!w || !h || !r) {
		wl_list_for_each(mode, &device->mode_list, link)
			if (mode->preferred)
				return mode;
		return !wl_list_empty(&device->mode_list) ?
			wl_container_of(device->mode_list.next, mode, link) :
			NULL;
	}

	wl_list_for_each(mode, &device->mode_list, link) {
		uint64_t diff = abs(mode->w-w) * abs(mode->h-h) * 1000 +
			abs(mode->refresh - r);

		if (w == mode->w && h == mode->h && r == mode->refresh)
			return mode;

		if (diff < min_diff) {
			min_diff = diff;
			matched = mode;
		}
	}
	return matched;
}

WL_EXPORT void
tw_output_device_set_scale(struct tw_output_device *device, float scale)
{
	if (scale <= 0) {
		tw_logl_level(TW_LOG_WARN, "attempt to set invalid scale %f"
		              " on output device", scale);
		return;
	}
	device->pending.scale = scale;
}

WL_EXPORT void
tw_output_device_set_transform(struct tw_output_device *device,
                               enum wl_output_transform transform)
{
	device->pending.transform = transform;
}

WL_EXPORT void
tw_output_device_enable(struct tw_output_device *device, bool enable)
{
	device->pending.enabled = enable;
}

WL_EXPORT void
tw_output_device_commit_state(struct tw_output_device *device)
{
	//emit for backend
	device->impl->commit_state(device);
	wl_signal_emit(&device->signals.commit_state, device);
	//emit for sending new backend info.
	wl_signal_emit(&device->signals.info, device);
}

WL_EXPORT void
tw_output_device_present(struct tw_output_device *device,
                         struct tw_event_output_device_present *event)
{
	uint32_t mhz = device->state.current_mode.refresh;
	struct tw_event_output_device_present _event = {
		.device = device,
	};
	struct timespec now;
	if (event == NULL) {
		event = &_event;
		clock_gettime(device->clk_id, &now);
		event->time = now;
	}
	event->refresh = tw_millihertz_to_ns(mhz);
	wl_signal_emit(&device->signals.present, event);
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

WL_EXPORT pixman_rectangle32_t
tw_output_device_geometry(const struct tw_output_device *output)
{
	int width, height;

	output_get_effective_resolution(&output->state, &width, &height);
	return (pixman_rectangle32_t){
		output->state.gx, output->state.gy,
		width, height
	};
}

WL_EXPORT void
tw_output_device_loc_to_global(const struct tw_output_device *output,
                               float x, float y, float *gx, float *gy)
{
	int width, height;

	output_get_effective_resolution(&output->state, &width, &height);

	*gx = output->state.gx + x * width;
	*gy = output->state.gy + y * height;
}

WL_EXPORT void
tw_output_device_raw_resolution(const struct tw_output_device *device,
                                unsigned *width, unsigned *height)
{
	*width = device->state.current_mode.w;
	*height = device->state.current_mode.h;
}
