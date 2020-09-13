/*
 * profiler.h - taiwins server profiler handler
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

#ifndef TW_PROFILER_H
#define TW_PROFILER_H

#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef  __cplusplus
extern "C" {
#endif

bool
tw_profiler_open(struct wl_display *display, const char *file);

void
tw_profiler_close();

void
tw_profiler_start_timer(const char *name);

void
tw_profiler_stop_timer(const char *name);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
