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
#include <ctypes/helpers.h>
#include "desktop.h"

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* the commands pass to layout algorithm */
enum layout_command {
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

/* the operation correspond to the command, I am not sure if it is good to have
 * responce immediately but currently I have no other way and when to apply the
 * operations is not clear either.
 */
struct layout_op {
	struct weston_view *v;
	//output
	struct weston_position pos;
	struct weston_size size;
	float scale;
	bool end;
	//input
	union {
		//resizing/moving parameters,
		struct {
			//dx,dy, delta
			float dx, dy;
			//surface x, surface y
			wl_fixed_t sx, sy;
		};
		struct weston_output *o;
		struct weston_geometry default_geometry;
	};
};

struct layout;
typedef void (*layout_fun_t)(const enum layout_command command,
                             const struct layout_op *arg,
                             struct weston_view *v, struct layout *l,
                             struct layout_op *ops);

//why I create this link based layout in the first place?
struct layout {
	bool clean;
	struct wl_list link;
	struct weston_layer *layer;
	layout_fun_t command;
	void *user_data; //this user_dat is useful
};

void
layout_init(struct layout *l, struct weston_layer *layer);

void
layout_release(struct layout *l);

void
layout_add_output(struct layout *l, struct tw_output *o);

void
layout_rm_output(struct layout *l, struct weston_output *o);

void
layout_resize_output(struct layout *l, struct tw_output *o);

void
floating_layout_init(struct layout *layout, struct weston_layer *ly);

void
floating_layout_end(struct layout *l);


void
tiling_layout_init(struct layout *layout, struct weston_layer *ly,
                   struct layout *floating);
void
tiling_layout_end(struct layout *l);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
