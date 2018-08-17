#include <stdlib.h>
#include <helpers.h>
#include "layout.h"

struct disposer_node {
	enum disposer_command command;
	disposer_fun_t fun;
};

//it will look like a long function, the easiest way is make an array which does
//the map, lucky the enum map is linear
static void
disposer_noop(const enum disposer_command command, const struct disposer_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct disposer_op *ops)
{
	ops[0].end = true;
}

void
layout_init(struct layout *l, struct weston_layer *layer, struct weston_output *o)
{
	*l = (struct layout){0};
	wl_list_init(&l->link);
	l->clean = true;
	l->layer = layer;
	l->output = o;
	l->nvisible = -1;
	l->commander = disposer_noop;
}

void
layout_release(struct layout *l)
{
	wl_list_remove(&l->link);
	*l = (struct layout){0};
}

struct floatlayout {
	struct layout layout;
	struct weston_geometry geo;
};

static void
disposer_float(const enum disposer_command command, const struct disposer_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct disposer_op *ops);

struct layout *
floatlayout_create(struct weston_layer *ly, struct weston_output *o)
{
	struct floatlayout *fl = malloc(sizeof(struct floatlayout));
	layout_init(&fl->layout, ly, o);
	fl->geo.x = o->x;
	fl->geo.y = o->y;
	fl->geo.width = o->width;
	fl->geo.height = o->height;
	fl->layout.commander = disposer_float;
	return (struct layout *)fl;
}

void
floatlayout_destroy(struct layout *l)
{
	struct floatlayout *fl = container_of(l, struct floatlayout, layout);
	layout_release(l);
	free(fl);
}

static void
floating_add(const enum disposer_command command, const struct disposer_op *arg,
	     struct weston_view *v, struct layout *l,
	     struct disposer_op *ops)
{
	assert(ops[0].end == 0);
	struct floatlayout *fl = container_of(l, struct floatlayout, layout);
	ops[0].pos.x = rand() % (fl->geo.width / 2);
	ops[0].pos.y = rand() % (fl->geo.height / 2);
	ops[0].end = false;
	ops[0].v = v;
	ops[1].end = 1;
}

static void
floating_deplace(const enum disposer_command command, const struct disposer_op *arg,
		 struct weston_view *v, struct layout *l,
		 struct disposer_op *ops)
{
	struct floatlayout *fl = container_of(l, struct floatlayout, layout);
	struct weston_position curr_pos = {
		v->geometry.x,
		v->geometry.y
	};
	if (command == DPSR_up)
		curr_pos.y -= 0.1 * fl->geo.height;
	else if (command == DPSR_down)
		curr_pos.y += 0.1 * fl->geo.height;
	else if (command == DPSR_left)
		curr_pos.x -= 0.1 * fl->geo.width;
	else if (command == DPSR_right)
		curr_pos.x += 0.1 * fl->geo.width;
	else {
		assert(!(arg[0].end));
		curr_pos = arg[0].pos;
	}
	//here is the delimma, we should maybe make the
	ops[0].pos = curr_pos;
	ops[0].end = false;
	ops[0].v = v;
	ops[1].end = 1;
}

static void
floating_resize(const enum disposer_command command, const struct disposer_op *arg,
		struct weston_view *v, struct layout *l,
		struct disposer_op *ops)
{
	ops[0].pos.x = -100000;
	ops[0].pos.y = -100000;
	ops[0].size = arg->size;
	ops[0].end = 0;
	ops[0].v = v;
	ops[1].end = 1;
}

static void
disposer_float(const enum disposer_command command, const struct disposer_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct disposer_op *ops)
{
	static struct disposer_node float_ops[] = {
		{DPSR_focus, disposer_noop},
		{DPSR_add, floating_add},
		{DPSR_del, disposer_noop},
		{DPSR_up, floating_deplace},
		{DPSR_down, floating_deplace},
		{DPSR_left, floating_deplace},
		{DPSR_right, floating_deplace},
		{DPSR_resize, floating_resize},
	};
	assert(float_ops[command].command == command);
	float_ops[command].fun(command, arg, v, l, ops);
}


void
layout_disposer(struct layout *l)
{
	if (l->clean)
		return;
	int nviews = 0;
	int nvisible;
	struct weston_position pos;
	struct weston_view *view;
	struct weston_layer *layer = l->layer;
	struct weston_view *views[wl_list_length(&layer->view_list.link)];
	nvisible = (l->nvisible > 0) ? l->nvisible : numof(views);

	wl_list_for_each(view, &layer->view_list.link, layer_link.link) {
		if (view->output == l->output || !l->output)
			views[nviews++] = view;
	}
	//if we force all the layout to run against
	for (int i = 0; i < nviews; i++) {
		//disable the views if we have enough data
		if (i >= nvisible) {
			weston_view_set_position(views[i], -1000.0, -1000.0);
			weston_view_set_mask(views[i], 0, 0, 0, 0);
			continue;
		}
		//TODO also you may want to change the size of the view, in that
		//case, you will want to weston_desktop functions
//		pos = l->disposer(views[i], l);
		weston_view_set_position(views[i], pos.x, pos.y);
		weston_view_schedule_repaint(views[i]);
	}
	l->clean = true;
	return;
}
