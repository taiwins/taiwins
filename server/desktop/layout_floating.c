/*
 * layout_floating.c - taiwins desktop floating layout implementation
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

#include <stdlib.h>
#include <pixman.h>

#include <ctypes/helpers.h>
#include <wayland-server-protocol.h>
#include <objects/surface.h>

#include "layout.h"
#include "workspace.h"

static void
emplace_float(const enum tw_xdg_layout_command command,
              const struct tw_xdg_layout_op *arg,
              struct tw_xdg_view *v, struct tw_xdg_layout *l,
              struct tw_xdg_layout_op *ops);

/******************************************************************************
 * floating layout
 *****************************************************************************/

void
tw_xdg_layout_init_floating(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_init(layout);
	layout->command = emplace_float;
	layout->type = LAYOUT_FLOATING;
}

void
tw_xdg_layout_end_floating(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_release(layout);
}


static void
floating_add(const enum tw_xdg_layout_command command,
             const struct tw_xdg_layout_op *arg,
             struct tw_xdg_view *v, struct tw_xdg_layout *l,
             struct tw_xdg_layout_op *ops)
{
	pixman_rectangle32_t output_geo = {
		v->output->output->state.x, v->output->output->state.y,
		v->output->output->state.w, v->output->output->state.h,
	};
	if (arg->in.default_geometry.width &&
	    arg->in.default_geometry.height) {
		ops[0].out.pos.x = arg->in.default_geometry.x;
		ops[0].out.pos.y = arg->in.default_geometry.y;
		ops[0].out.size.width = arg->in.default_geometry.width;
		ops[0].out.size.height = arg->in.default_geometry.height;
	} else {
		ops[0].out.pos.x = output_geo.x + output_geo.width / 2.0 -
			v->dsurf->window_geometry.w/2.0;
		ops[0].out.pos.y = output_geo.y + output_geo.height / 2.0 -
			v->dsurf->window_geometry.h/2.0;
		//so this is invalid size
		ops[0].out.size.width = 0;
		ops[0].out.size.height = 0;
	}
	assert(!ops[0].out.end);
	ops[0].out.end = false;
	ops[0].v = v;
	ops[1].out.end = 1;
}

static void
floating_deplace(const enum tw_xdg_layout_command command,
                 const struct tw_xdg_layout_op *arg,
                 struct tw_xdg_view *v, struct tw_xdg_layout *l,
                 struct tw_xdg_layout_op *ops)
{
	int32_t gx = v->x;
	int32_t gy = v->y;

	gx += arg->in.dx;
	gy += arg->in.dy;
	//here is the delimma, we should maybe make the
	ops[0].out.pos.x = gx;
	ops[0].out.pos.y = gy;
	ops[0].out.end = false;
	ops[0].v = v;
	ops[1].out.end = 1;
}

static void
floating_resize(const enum tw_xdg_layout_command command,
                const struct tw_xdg_layout_op *arg,
                struct tw_xdg_view *v, struct tw_xdg_layout *l,
                struct tw_xdg_layout_op *ops)
{
	//set position unchanged
	//we are adding visible.xy here because we will subtract
	ops[0].out.pos.x = v->x;
	ops[0].out.pos.y = v->y;
	ops[0].out.size.width = v->dsurf->window_geometry.w;
	ops[0].out.size.height = v->dsurf->window_geometry.h;
	if (arg->in.edge & WL_SHELL_SURFACE_RESIZE_TOP) {
		ops[0].out.size.height -= arg->in.dy;
		ops[0].out.pos.y += arg->in.dy;
	}
	if (arg->in.edge & WL_SHELL_SURFACE_RESIZE_BOTTOM)
		ops[0].out.size.height += arg->in.dy;
	if (arg->in.edge & WL_SHELL_SURFACE_RESIZE_LEFT) {
		ops[0].out.size.width -= arg->in.dx;
		ops[0].out.pos.x += arg->in.dx;
	}
	if (arg->in.edge & WL_SHELL_SURFACE_RESIZE_RIGHT)
		ops[0].out.size.width += arg->in.dx;

	ops[0].v = v;
	ops[0].out.end = false;
	ops[1].out.end = true;
}

void
emplace_float(const enum tw_xdg_layout_command command,
              const struct tw_xdg_layout_op *arg,
              struct tw_xdg_view *v, struct tw_xdg_layout *l,
              struct tw_xdg_layout_op *ops)
{
	struct placement_node {
		enum tw_xdg_layout_command command;
		tw_xdg_layout_fun_t fun;
	};

	static struct placement_node float_ops[] = {
		{DPSR_focus, tw_xdg_layout_emplace_noop},
		{DPSR_add, floating_add},
		{DPSR_del, tw_xdg_layout_emplace_noop},
		{DPSR_deplace, floating_deplace},
		{DPSR_toggle, tw_xdg_layout_emplace_noop},
		{DPSR_resize, floating_resize},
		{DPSR_vsplit, tw_xdg_layout_emplace_noop},
		{DPSR_hsplit, tw_xdg_layout_emplace_noop},
		{DPSR_merge, tw_xdg_layout_emplace_noop},
		{DPSR_output_add, tw_xdg_layout_emplace_noop},
		{DPSR_output_rm, tw_xdg_layout_emplace_noop},
		{DPSR_output_resize, tw_xdg_layout_emplace_noop},
	};
	assert(float_ops[command].command == command);
	float_ops[command].fun(command, arg, v, l, ops);
}
