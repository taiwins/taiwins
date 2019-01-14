#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include <libweston-desktop.h>
#include "layout.h"
#include "workspace.h"

static void
emplace_float(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops);

static void
emplace_noop(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	ops[0].end = true;
}



////////////////////////////////////////////////////////////////////////////////
///////////////////////////// floating layout //////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


void
floating_layout_init(struct layout *layout, struct weston_layer *layer)
{
	layout_init(layout, layer);
	layout->command = emplace_float;
}

void floating_layout_end(struct layout *layout)
{
	layout_release(layout);
}


static void
floating_add(const enum layout_command command, const struct layout_op *arg,
	     struct weston_view *v, struct layout *l,
	     struct layout_op *ops)
{
	struct weston_geometry geo = {
		v->output->x, v->output->y,
		v->output->width, v->output->height,
	};

	assert(!ops[0].end);
	ops[0].pos.x = rand() % (geo.width / 2);
	ops[0].pos.y = rand() % (geo.height / 2);
	//so this is invalid size
	ops[0].size.width = 0;
	ops[0].size.height = 0;

	ops[0].end = false;
	ops[0].v = v;
	ops[1].end = 1;
}

static void
floating_deplace(const enum layout_command command, const struct layout_op *arg,
		 struct weston_view *v, struct layout *l,
		 struct layout_op *ops)
{
	struct weston_position curr_pos = {
		v->geometry.x,
		v->geometry.y
	};
	curr_pos.x += arg->dx;
	curr_pos.y += arg->dy;
	//here is the delimma, we should maybe make the
	ops[0].pos = curr_pos;
	ops[0].end = false;
	ops[0].v = v;
	ops[1].end = 1;
}

static void
floating_resize(const enum layout_command command, const struct layout_op *arg,
		struct weston_view *v, struct layout *l,
		struct layout_op *ops)
{
	struct weston_geometry geo = get_recent_view(v)->old_geometry;
	struct weston_geometry buttom_right = {
		.x = v->geometry.x + geo.x + geo.width,
		.y = v->geometry.y + geo.y + geo.height,
	};

	ops[0].pos.x = v->geometry.x + geo.x;
	ops[0].pos.y = v->geometry.y + geo.y;
	ops[0].v = v;
	ops[0].end = false;

	float x, y, rx, ry;
	weston_view_from_global_float(v, arg->sx, arg->sy, &x, &y);
	rx = x / (float)v->surface->width;
	ry = y / (float)v->surface->height;
	//only the buttom right part does not affect the position
	if (rx < 0.5 || ry < 0.5) {
		ops[0].pos.x += arg->dx;
		ops[0].pos.y += arg->dy;
		ops[0].size.width = buttom_right.x - (int32_t)ops[0].pos.x;
		ops[0].size.height = buttom_right.y - (int32_t)ops[0].pos.y;
	} else {
		ops[0].size.width = geo.width + (int32_t)arg->dx;
		ops[0].size.height = geo.height + (int32_t)arg->dy;
	}
	ops[1].end = true;
}

void
emplace_float(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops)
{
	struct placement_node {
		enum layout_command command;
		layout_fun_t fun;
	};

	static struct placement_node float_ops[] = {
		{DPSR_focus, emplace_noop},
		{DPSR_add, floating_add},
		{DPSR_del, emplace_noop},
		{DPSR_deplace, floating_deplace},
		{DPSR_toggle, emplace_noop},
		{DPSR_resize, floating_resize},
	};
	assert(float_ops[command].command == command);
	float_ops[command].fun(command, arg, v, l, ops);
}
