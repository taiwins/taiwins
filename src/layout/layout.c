#include <stdlib.h>
#include <helpers.h>
#include "layout.h"


struct floatlayout {
	struct layout;
	struct weston_geometry geo;
};


struct weston_position
layout_floatpos(struct weston_view *view, struct layout *l)
{
	struct weston_position pos;
	struct floatlayout *fl = l;
	if (view->geometry.x >= fl->geo.x &&
	    view->geometry.x < fl->geo.x + fl->geo.width &&
	    view->geometry.y >= fl->geo.y &&
	    view->geometry.y < fl->geo.y + fl->geo.height)
		return pos;
	else {
		pos.x = rand() % (fl->geo.width  / 2);
		pos.y = rand() % (fl->geo.height / 2);
	}
	return pos;
}

//this design pose the problem, since you have to run this for all the views in
//the layer.
struct weston_position
layout_master(struct weston_view *view, struct layout *l)
{
	//I don't know what is the order of the current view, I can rely on the
	//position of current view in the layer?
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
		pos = l->disposer(views[i], l);
		weston_view_set_position(views[i], pos.x, pos.y);
		weston_view_schedule_repaint(views[i]);
	}
	l->clean = true;
	return;
}
