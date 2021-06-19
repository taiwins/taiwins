/*
 * config_types.h - taiwins config types collection
 *
 * Copyright (c) 2019 Xichen Zhou
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

#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stdbool.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include "desktop/xdg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int rotate;
	bool flip;
	enum wl_output_transform t;
} tw_config_transform_t;

typedef OPTION(enum wl_output_transform, transform) pending_transform_t;
typedef OPTION(enum tw_layout_type, layout) pending_layout_t;
typedef OPTION(bool, enable) pending_boolean_t;
typedef OPTION(enum taiwins_shell_task_switch_effect, eff) pending_effect_t;
typedef OPTION(enum taiwins_shell_panel_pos, pos) pending_panel_pos_t;
typedef OPTION(int32_t, val) pending_intval_t;
typedef OPTION(uint32_t, uval) pending_uintval_t;
typedef OPTION(struct tw_theme *, theme) pending_theme_t;

#define SET_PENDING(ptr, name, value)                                   \
	do { \
		(ptr)->name = value; \
		(ptr)->valid = true; \
	} while (0)

struct tw_config_output {
	char name[24];
	pending_intval_t scale, posx, posy;
	pending_uintval_t width, height;
	pending_transform_t transform;
	pending_boolean_t enabled, primary;
};

#ifdef __cplusplus
}
#endif

#endif /* EOF */
