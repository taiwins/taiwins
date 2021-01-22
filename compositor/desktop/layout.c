/*
 * layout.c - taiwins desktop layout implementation
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

#include <stdlib.h>
#include <ctypes/helpers.h>
#include <ctypes/sequential.h>
#include <ctypes/tree.h>
#include <wayland-util.h>
#include <pixman.h>

#include "xdg.h"
#include "layout.h"


//it will look like a long function, the easiest way is make an array which does
//the map, lucky the enum map is linear
void
tw_xdg_layout_emplace_noop(const enum tw_xdg_layout_command command,
                           const struct tw_xdg_layout_op *arg,
                           struct tw_xdg_view *v, struct tw_xdg_layout *l,
                           struct tw_xdg_layout_op *ops)
{
	ops[0].out.end = true;
}

void
emplace_tiling(const enum tw_xdg_layout_command command,
               const struct tw_xdg_layout_op *arg, struct tw_xdg_view *v,
               struct tw_xdg_layout *l, struct tw_xdg_layout_op *ops);
void
emplace_float(const enum tw_xdg_layout_command command,
              const struct tw_xdg_layout_op *arg,  struct tw_xdg_view *v,
              struct tw_xdg_layout *l, struct tw_xdg_layout_op *ops);


void
tw_xdg_layout_init(struct tw_xdg_layout *l)
{
	*l = (struct tw_xdg_layout){0};
	for (int i = 0; i < MAX_WORKSPACES; i++)
		wl_list_init(&l->links[i]);
	l->clean = true;
	l->command = tw_xdg_layout_emplace_noop;
	l->user_data = NULL;
}

void
tw_xdg_layout_release(struct tw_xdg_layout *l)
{
	*l = (struct tw_xdg_layout){0};
}

void
tw_xdg_layout_add_output(struct tw_xdg_layout *l, struct tw_xdg_output *o)
{
	/* if (l->command == emplace_tiling) */
	/*	tiling_add_output(l, o); */
	struct tw_xdg_layout_op op = {
		.in.o = o,
	};
	l->command(DPSR_output_add, &op, NULL, l, NULL);
}

void
tw_xdg_layout_rm_output(struct tw_xdg_layout *l, struct tw_xdg_output *o)
{
	/* if (l->command == emplace_tiling) */
	/*	tiling_rm_output(l, o); */
	struct tw_xdg_layout_op op = {
		.in.o = o,
	};
	l->command(DPSR_output_rm, &op, NULL, l, NULL);
}

void
tw_xdg_layout_resize_output(struct tw_xdg_layout *l, struct tw_xdg_output *o)
{
	struct tw_xdg_layout_op op = {
		.in.o = o,
	};
	l->command(DPSR_output_resize, &op, NULL, l, NULL);
	/* if (l->command == emplace_tiling) */
	/*	tiling_resize_output(l, o); */
}
