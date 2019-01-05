#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include <tree.h>
#include "layout.h"



//it will look like a long function, the easiest way is make an array which does
//the map, lucky the enum map is linear
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

extern void tiling_add_output(struct layout *l, struct weston_output *o);
extern void tiling_rm_output(struct layout *l, struct weston_output *o);


void
emplace_float(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops);


void
layout_init(struct layout *l, struct weston_layer *layer)
{
	*l = (struct layout){0};
	wl_list_init(&l->link);
	l->clean = true;
	l->layer = layer;
	l->command = emplace_noop;
	l->user_data = NULL;
}

void
layout_release(struct layout *l)
{
	*l = (struct layout){0};
}

void
layout_add_output(struct layout *l, struct weston_output *o)
{
	if (l->command == emplace_tiling)
		tiling_add_output(l, o);
}

void
layout_rm_output(struct layout *l, struct weston_output *o)
{
	if (l->command == emplace_tiling)
		tiling_rm_output(l, o);
}
