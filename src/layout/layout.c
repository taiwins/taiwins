#include <stdlib.h>
#include <helpers.h>
#include "layout.h"


struct floatlayout {
	struct layout layout;
	struct weston_geometry geo;
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



static void
disposer_float(const enum disposer_command command, const struct disposer_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct disposer_op *ops)
{
	int end = 0;
	struct floatlayout *fl = container_of(l, struct floatlayout, layout);
	switch (command) {
	case DPSR_add:
		ops->pos.x = rand() % (fl->geo.width / 2);
		ops->pos.y = rand() % (fl->geo.height / 2);
		ops->end = false;
		ops->v = v;
		end = 1;
	default:
	/* case DPSR_up: */
	/* case DPSR_down: */
	/* case DPSR_focus: */
		break;
	}
	ops[end].end = true;
	return;
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
