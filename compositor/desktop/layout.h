/*
 * layout.h - taiwins desktop layout header
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

#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>
#include <wayland-server.h>
#include <pixman.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/surface.h>

#include "xdg.h"

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* the commands pass to layout algorithm */
enum tw_xdg_layout_command {
	DPSR_focus, //useful in floating.
	DPSR_add,
	DPSR_del,
	DPSR_deplace, //useful in floating.
	DPSR_toggle, //toggle subspace into vertical or horizental
	DPSR_resize, //useful in tiling
	DPSR_vsplit,
	DPSR_hsplit,
	DPSR_merge,
	DPSR_output_add,
	DPSR_output_rm,
	DPSR_output_resize,
};

enum tw_xdg_view_state {
	TW_XDG_VIEW_TILED_LEFT = 1 << 0,
	TW_XDG_VIEW_TILED_RIGHT = 1 << 1,
	TW_XDG_VIEW_TILED_TOP = 1 << 2,
	TW_XDG_VIEW_TILED_BOTTOM = 1 << 3,
	TW_XDG_VIEW_FOCUSED = 1 << 4,
};

struct tw_xdg_view;
struct tw_xdg_layout_op {
	struct tw_xdg_view *v, *focused;
	//output
	struct {
		struct { int32_t x, y; } pos;
		struct { uint32_t width, height; } size;
		float scale;
		bool end;
		uint32_t state;
	} out;
	//input
	union {
		//resizing/moving parameters,
		struct {
			//dx,dy, delta
			float dx, dy;
			enum wl_shell_surface_resize edge;
			//surface x, surface y
			wl_fixed_t sx, sy;
		};
		struct tw_xdg_output *o;
		pixman_rectangle32_t default_geometry;
	} in;
};

struct tw_xdg_layout;
typedef void (*tw_xdg_layout_fun_t)(const enum tw_xdg_layout_command command,
                                    const struct tw_xdg_layout_op *arg,
                                    struct tw_xdg_view *v,
                                    struct tw_xdg_layout *l,
                                    struct tw_xdg_layout_op *ops);

//why I create this link based layout in the first place?
struct tw_xdg_layout {
	bool clean;
	struct wl_list links[MAX_WORKSPACES];
	enum tw_layout_type type;
	tw_xdg_layout_fun_t command;
	void *user_data; //this user_dat is useful
};

void
tw_xdg_layout_init(struct tw_xdg_layout *l);

void
tw_xdg_layout_release(struct tw_xdg_layout *l);

void
tw_xdg_layout_add_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
tw_xdg_layout_rm_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
tw_xdg_layout_resize_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
tw_xdg_layout_init_floating(struct tw_xdg_layout *tw_xdg_layout);

void
tw_xdg_layout_end_floating(struct tw_xdg_layout *l);

void
tw_xdg_layout_init_maximized(struct tw_xdg_layout *layout);

void
tw_xdg_layout_end_maximized(struct tw_xdg_layout *layout);

void
tw_xdg_layout_init_fullscreen(struct tw_xdg_layout *layout);

void
tw_xdg_layout_end_fullscreen(struct tw_xdg_layout *layout);

void
tw_xdg_layout_init_tiling(struct tw_xdg_layout *layout);

void
tw_xdg_layout_end_tiling(struct tw_xdg_layout *layout);

void
tw_xdg_layout_emplace_noop(const enum tw_xdg_layout_command command,
                           const struct tw_xdg_layout_op *arg,
                           struct tw_xdg_view *v, struct tw_xdg_layout *l,
                           struct tw_xdg_layout_op *ops);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
