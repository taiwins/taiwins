#include <stdlib.h>
#include <helpers.h>
#include "layout.h"


struct floatlayout {
	struct layout layout;
	struct weston_geometry geo;
};


struct weston_position
layout_floatpos(struct weston_view *view, struct layout *l)
{
	//I am not sure if this is right implementation
	struct floatlayout *fl = container_of(l, struct floatlayout, layout);
	struct weston_position pos = {
		.x = view->geometry.x,
		.y = view->geometry.y
	};
	if (is_inbound(pos.x, fl->geo.x, fl->geo.x + fl->geo.width) &&
	    is_inbound(pos.y, fl->geo.y, fl->geo.y + fl->geo.height))
		return pos;
	else {
		pos.x = rand() % (fl->geo.width  / 2);
		pos.y = rand() % (fl->geo.height / 2);
	}
	return pos;
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
		pos = l->disposer(views[i], l);
		weston_view_set_position(views[i], pos.x, pos.y);
		weston_view_schedule_repaint(views[i]);
	}
	l->clean = true;
	return;
}
