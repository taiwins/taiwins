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
#include <objects/layers.h>
#include <objects/surface.h>

#include "xdg.h"

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

enum tw_layout_type {
	LAYOUT_FLOATING,
	LAYOUT_TILING,
	LAYOUT_MAXMIZED,
	LAYOUT_FULLSCREEN,
};

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
	DPSR_output_resize,
};

struct tw_xdg_view;
struct tw_xdg_layout_op {
	struct tw_xdg_view *v;
	//output
	struct {
		struct { int32_t x, y; } pos;
		struct { uint32_t width, height; } size;
		float scale;
		bool end;
	} out;
	//input
	union {
		//resizing/moving parameters,
		struct {
			//dx,dy, delta
			float dx, dy;
			//surface x, surface y
			wl_fixed_t sx, sy;
		};
		struct tw_xdg_output *o;
		pixman_rectangle32_t default_geometry;
	} in;
};

struct tw_xdg_layout;
typedef void (*layout_fun_t)(const enum tw_xdg_layout_command command,
                             const struct tw_xdg_layout_op *arg,
                             struct tw_xdg_view *v, struct tw_xdg_layout *l,
                             struct tw_xdg_layout_op *ops);

//why I create this link based layout in the first place?
struct tw_xdg_layout {
	bool clean;
	struct wl_list links[MAX_WORKSPACES];
	struct tw_layer *layer;
	enum tw_layout_type type;
	layout_fun_t command;
	void *user_data; //this user_dat is useful
};

void
tw_xdg_layout_init(struct tw_xdg_layout *l, struct tw_layer *layer);

void
tw_xdg_layout_release(struct tw_xdg_layout *l);

void
tw_xdg_layout_add_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
tw_xdg_layout_rm_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
tw_xdg_layout_resize_output(struct tw_xdg_layout *l, struct tw_xdg_output *o);

void
floating_tw_xdg_layout_init(struct tw_xdg_layout *tw_xdg_layout,
                            struct tw_layer *ly);

void
floating_tw_xdg_layout_end(struct tw_xdg_layout *l);


void
tiling_tw_xdg_layout_init(struct tw_xdg_layout *tw_xdg_layout,
                          struct tw_layer *ly,
                   struct tw_xdg_layout *floating);
void
tiling_tw_xdg_layout_end(struct tw_xdg_layout *l);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
