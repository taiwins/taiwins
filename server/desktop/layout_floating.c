#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include "layout.h"


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
	struct weston_output *o = v->output;
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
	//what the hell is this
	ops[0].pos.x = -100000;
	ops[0].pos.y = -100000;
	ops[0].size = arg->size;
	ops[0].end = 0;
	ops[0].v = v;
	ops[1].end = 1;
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
