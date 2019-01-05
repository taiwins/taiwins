#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include "layout.h"


static void
emplace_noop(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	ops[0].end = true;
}

void
emplace_tiling(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops);


////////////////////////////////////////////////////////////////////////////////
////////////////////////////// tining layout ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


void
tiling_layout_init(struct layout *l, struct weston_layer *ly)
{
	layout_init(l, ly);
	l->user_data = xmalloc(sizeof(vector_t));
	l->command = emplace_tiling;
	//change this, now you have to consider the option that someone unplug
	//the monitor
	vector_init(l->user_data, sizeof(struct vtree_node *), NULL);
}

void
tiling_layout_end(struct layout *l)
{
	layout_release(l);
	vector_destroy(l->user_data);
	free(l->user_data);
}


void tiling_add_output(struct layout *l, struct weston_output *o)
{
}

void tiling_rm_output(struct layout *l, struct weston_output *o)
{}


void
emplace_tiling(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops)
{
	//make a dummy one now
	struct placement_node {
		enum layout_command command;
		layout_fun_t fun;
	};

	static struct placement_node t_ops[] = {
		{DPSR_focus, emplace_noop},
		{DPSR_add, emplace_noop},
		{DPSR_del, emplace_noop},
		{DPSR_deplace, emplace_noop},
		{DPSR_up, emplace_noop},
		{DPSR_down, emplace_noop},
		{DPSR_left, emplace_noop},
		{DPSR_right, emplace_noop},
		{DPSR_resize, emplace_noop},
	};
	assert(t_ops[command].command == command);
	t_ops[command].fun(command, arg, v, l, ops);
}
