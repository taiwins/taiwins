/*
 * output_device.h - taiwins server output device internal header
 *
 * Copyright (c) 2021 Xichen Zhou
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

#ifndef TW_OUTPUT_DEVICE_INTERNAL_H
#define TW_OUTPUT_DEVICE_INTERNAL_H

#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/matrix.h>
#include <taiwins/output_device.h>

#ifdef  __cplusplus
extern "C" {
#endif

void
tw_output_device_init(struct tw_output_device *device,
                      const struct tw_output_device_impl *impl);
void
tw_output_device_fini(struct tw_output_device *device);

struct tw_output_device_mode *
tw_output_device_match_mode(struct tw_output_device *device,
                            int width, int height, int refresh);
static inline bool
tw_output_device_mode_match(const struct tw_output_device_mode *mode,
                         int w, int h, int r)
{
	return mode->w == w && mode->h == h && mode->refresh == r;
}

static inline bool
tw_output_device_mode_eq(const struct tw_output_device_mode *a,
                         const struct tw_output_device_mode *b)
{
	return tw_output_device_mode_match(a, b->w, b->h, b->refresh);
}

static inline bool
tw_output_device_state_eq(const struct tw_output_device_state *a,
                          const struct tw_output_device_state *b)
{
	return a->enabled == b->enabled &&
		a->scale == b->scale &&
		a->gx == b->gx &&
		a->gy == b->gy &&
		a->transform == b->transform &&
		tw_output_device_mode_eq(&a->current_mode, &b->current_mode);
}

static inline void
tw_output_device_set_current_mode(struct tw_output_device *device,
                                  unsigned width, unsigned height, int refresh)
{
	device->current.current_mode.h = height;
	device->current.current_mode.w = width;
	device->current.current_mode.refresh = refresh;
	wl_signal_emit(&device->signals.commit_state, device);
}

static inline void
tw_output_device_reset_clock(struct tw_output_device *device, clockid_t clk)
{
	device->clk_id = clk;
	wl_signal_emit(&device->signals.clock_reset, device);
}

/* TODO determine the id inside backend */
static inline void
tw_output_device_set_id(struct tw_output_device *device, int id)
{
	device->id = id;
}


#ifdef  __cplusplus
}
#endif

#endif
