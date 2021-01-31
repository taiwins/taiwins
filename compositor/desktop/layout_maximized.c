/*
 * layout_maximized.c - taiwins desktop maximized layout implementation
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
#include <taiwins/output_device.h>

#include "layout.h"
#include "workspace.h"
#include "xdg.h"

static void
emplace_maximized(const enum tw_xdg_layout_command command,
                  const struct tw_xdg_layout_op *arg,
                  struct tw_xdg_view *v, struct tw_xdg_layout *l,
                  struct tw_xdg_layout_op *ops);

/******************************************************************************
 * maximized layout
 *****************************************************************************/

void
tw_xdg_layout_init_maximized(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_init(layout);
	layout->command = emplace_maximized;
	layout->type = LAYOUT_MAXIMIZED;
}

void
tw_xdg_layout_end_maximized(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_release(layout);
}

static void
maximized_add(const enum tw_xdg_layout_command command,
             const struct tw_xdg_layout_op *arg,
             struct tw_xdg_view *v, struct tw_xdg_layout *l,
             struct tw_xdg_layout_op *ops)
{
	pixman_rectangle32_t output_geo =
		tw_output_device_geometry(v->output->output->device);

	if (arg->in.default_geometry.width &&
	    arg->in.default_geometry.height) {
		ops[0].out.pos.x = arg->in.default_geometry.x;
		ops[0].out.pos.y = arg->in.default_geometry.y;
		ops[0].out.size.width = arg->in.default_geometry.width;
		ops[0].out.size.height = arg->in.default_geometry.height;
	} else {
		ops[0].out.pos.x = output_geo.x;
		ops[0].out.pos.y = output_geo.y;
		ops[0].out.size.width = output_geo.width;
		ops[0].out.size.height = output_geo.height;
	}
	assert(!ops[0].out.end);
	ops[0].out.end = false;
	ops[0].v = v;
	ops[1].out.end = 1;
}

static void
emplace_maximized(const enum tw_xdg_layout_command command,
                  const struct tw_xdg_layout_op *arg,
                  struct tw_xdg_view *v, struct tw_xdg_layout *l,
                  struct tw_xdg_layout_op *ops)
{
	struct placement_node {
		enum tw_xdg_layout_command command;
		tw_xdg_layout_fun_t fun;
	};

	static struct placement_node maximized_ops[] = {
		{DPSR_focus, tw_xdg_layout_emplace_noop},
		{DPSR_add, maximized_add},
		{DPSR_del, tw_xdg_layout_emplace_noop},
		{DPSR_deplace, tw_xdg_layout_emplace_noop},
		{DPSR_toggle, tw_xdg_layout_emplace_noop},
		{DPSR_resize, tw_xdg_layout_emplace_noop},
		{DPSR_vsplit, tw_xdg_layout_emplace_noop},
		{DPSR_hsplit, tw_xdg_layout_emplace_noop},
		{DPSR_merge, tw_xdg_layout_emplace_noop},
		{DPSR_output_add, tw_xdg_layout_emplace_noop},
		{DPSR_output_rm, tw_xdg_layout_emplace_noop},
		{DPSR_output_resize, tw_xdg_layout_emplace_noop},
	};
	assert(maximized_ops[command].command == command);
	maximized_ops[command].fun(command, arg, v, l, ops);
}
