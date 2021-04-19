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

#ifndef TW_PROFILING_H
#define TW_PROFILING_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <taiwins/objects/profiler.h>

#ifdef  __cplusplus
extern "C" {
#endif

#if _TW_ENABLE_PROFILING

#define SCOPE_PROFILE_BEG() tw_profiler_start_timer(__func__)
#define SCOPE_PROFILE_END() tw_profiler_stop_timer(__func__)
#define PROFILE_BEG(name) tw_profiler_start_timer(name)
#define PROFILE_END(name) tw_profiler_stop_timer(name)
#define SCOPE_PROFILE_TS() tw_profiler_timestamp(__func__)

#else

#define SCOPE_PROFILE_BEG()
#define SCOPE_PROFILE_END()
#define PROFILE_BEG(name)
#define PROFILE_END(name)
#define SCOPE_PROFILE_TS()
#endif

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
