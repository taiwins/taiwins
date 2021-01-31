/*
 * layout_fullscreen.c - taiwins desktop fullscreen layout implementation
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

#include "layout.h"
#include "workspace.h"
#include "xdg.h"

static void
emplace_fullscreen(const enum tw_xdg_layout_command command,
                   const struct tw_xdg_layout_op *arg,
                   struct tw_xdg_view *v, struct tw_xdg_layout *l,
                   struct tw_xdg_layout_op *ops);

/******************************************************************************
 * fullscreen layout
 *****************************************************************************/

void
tw_xdg_layout_init_fullscreen(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_init(layout);
	layout->command = emplace_fullscreen;
	layout->type = LAYOUT_FULLSCREEN;
}

void
tw_xdg_layout_end_fullscreen(struct tw_xdg_layout *layout)
{
	tw_xdg_layout_release(layout);
}

static void
fullscreen_add(const enum tw_xdg_layout_command command,
             const struct tw_xdg_layout_op *arg,
             struct tw_xdg_view *v, struct tw_xdg_layout *l,
             struct tw_xdg_layout_op *ops)
{
	pixman_rectangle32_t output_geo =
		tw_output_device_geometry(v->output->output->device);

	ops[0].out.pos.x = output_geo.x;
	ops[0].out.pos.y = output_geo.y;
	ops[0].out.size.width = output_geo.width;
	ops[0].out.size.height = output_geo.height;

	assert(!ops[0].out.end);
	ops[0].out.end = false;
	ops[0].v = v;
	ops[1].out.end = 1;
}

static void
emplace_fullscreen(const enum tw_xdg_layout_command command,
                  const struct tw_xdg_layout_op *arg,
                  struct tw_xdg_view *v, struct tw_xdg_layout *l,
                  struct tw_xdg_layout_op *ops)
{
	struct placement_node {
		enum tw_xdg_layout_command command;
		tw_xdg_layout_fun_t fun;
	};

	static struct placement_node fullscreen_ops[] = {
		{DPSR_focus, tw_xdg_layout_emplace_noop},
		{DPSR_add, fullscreen_add},
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
	assert(fullscreen_ops[command].command == command);
	fullscreen_ops[command].fun(command, arg, v, l, ops);
}
