#include <string.h>
#include <stdlib.h>
#include "workspace.h"
#include "layout.h"


struct workspace {
	struct wl_list floating_layout_link;
	struct wl_list tiling_layout_link;
	//workspace does not distinguish the outputs.
	//so when we `switch_workspace, all the output has to update.
	//The layouting algorithm may have to worry about output
	struct weston_layer hidden_layer;
	struct weston_layer tiling_layer;
	struct weston_layer floating_layer;
	//Recent views::
	//we need a recent_views struct for user to switch among views. FIRST a
	//link list would be ideal but weston view struct does not have a link
	//for it. The SECOND best choice is a link-list that wraps the view in
	//it, but this requires extensive memory allocation. The NEXT best thing
	//is a stack. Since the recent views and stack share the same logic. We
	//will need a unique stack which can eliminate the duplicated elements.

	//the only tiling layer here will create the problem when we want to do
	//the stacking layout, for example. Only show two views.
};

size_t workspace_size =  sizeof(struct workspace);




/**
 * workspace implementation
 */
void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	wl_list_init(&wp->floating_layout_link);
	wl_list_init(&wp->tiling_layout_link);
	weston_layer_init(&wp->tiling_layer, compositor);
	weston_layer_init(&wp->floating_layer, compositor);
	weston_layer_init(&wp->hidden_layer, compositor);
}

void
workspace_release(struct workspace *ws)
{
	struct weston_view *view, *next;
	struct weston_layer *layers[3]  = {
		&ws->floating_layer,
		&ws->tiling_layer,
		&ws->hidden_layer,
	};
	//get rid of all the surface, maybe
	for (int i = 0; i < 3; i++) {
		if (!wl_list_length(&layers[i]->view_list.link))
			continue;
		wl_list_for_each_safe(view, next,
				      &layers[i]->view_list.link,
				      layer_link.link) {
			struct weston_surface *surface =
				weston_surface_get_main_surface(view->surface);
			weston_surface_destroy(surface);
		}
	}
}


static struct layout *
workspace_get_layout_for_view(const struct workspace *ws, const struct weston_view *v)
{
	struct layout *l;
	if (!v || (v->layer_link.layer != &ws->floating_layer &&
		   v->layer_link.layer != &ws->tiling_layer))
		return NULL;
	wl_list_for_each(l, &ws->floating_layout_link, link) {
		if (l->layer == &ws->floating_layer)
			return l;
	}
	wl_list_for_each(l, &ws->tiling_layout_link, link) {
		if (l->layer == &ws->tiling_layer)
			return l;
	}
	return NULL;
}

void
arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
			const enum disposer_command command,
			const struct disposer_op *arg)
{
	struct layout *layout = workspace_get_layout_for_view(ws, v);
	if (!layout)
		return;
	int len = wl_list_length(&ws->floating_layer.view_list.link) +
		wl_list_length(&ws->tiling_layer.view_list.link) + 1;
	struct disposer_op ops[len];
	memset(ops, 0, sizeof(ops));
	layout->commander(command, arg, v, layout, ops);
	for (int i = 0; i < len; i++) {
		if (ops[i].end)
			break;
		//TODO, check the validty of the operations
		weston_view_set_position(v, ops[i].pos.x, ops[i].pos.y);
		weston_view_schedule_repaint(v);
	}
}


void
workspace_switch(struct workspace *to, struct workspace *from)
{
	weston_layer_unset_position(&from->floating_layer);
	weston_layer_unset_position(&from->tiling_layer);

	weston_layer_set_position(&to->tiling_layer, WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&to->floating_layer , WESTON_LAYER_POSITION_NORMAL+1);
}


static void
workspace_clear_floating(struct workspace *ws)
{
	struct weston_view *view, *next;
	if (wl_list_length(&ws->floating_layer.view_list.link))
		wl_list_for_each_safe(view, next,
				      &ws->floating_layer.view_list.link,
				      layer_link.link) {
			weston_layer_entry_remove(&view->layer_link);
			weston_layer_entry_insert(&ws->hidden_layer.view_list,
						  &view->layer_link);
		}
}

bool
workspace_focus_view(struct workspace *ws, struct weston_view *v)
{
	struct weston_layer *l = (v) ? v->layer_link.layer : NULL;
	if (!l || (l != &ws->floating_layer && l != &ws->tiling_layer))
		return false;
	//move float to hidden is a bit tricky
	if (l == &ws->floating_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);
		//we should somehow ping on it
	} else if (l == &ws->tiling_layer) {
		workspace_clear_floating(ws);
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->tiling_layer.view_list,
					  &v->layer_link);
	} else if (l == &ws->hidden_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);

	}
	weston_view_damage_below(v);
	weston_view_schedule_repaint(v);
	return true;
	//it is not possible to be here.
}


void
workspace_add_output(struct workspace *wp, struct weston_output *output)
{
	if (wl_list_empty(&wp->floating_layout_link)) {
		struct layout *fl = floatlayout_create(&wp->floating_layer, output);
		wl_list_insert(&wp->floating_layout_link, &fl->link);
	}
	//TODO create the tiling_layout as well.
}

void
workspace_remove_output(struct workspace *w, struct weston_output *output)
{
	struct layout *l, *next;
	wl_list_for_each_safe(l, next, &w->floating_layout_link, link) {
		wl_list_remove(&l->link);
		floatlayout_destroy(l);
		//some how you need to move all the layer here
	}
	wl_list_for_each_safe(l, next, &w->tiling_layout_link, link) {
	}
}


bool
is_view_on_workspace(const struct weston_view *v, const struct workspace *ws)
{
	const struct weston_layer *layer = v->layer_link.layer;
	return (layer == &ws->floating_layer || layer == &ws->tiling_layer);

}

void
workspace_add_view(struct workspace *w, struct weston_view *view)
{
	if (wl_list_empty(&view->layer_link.link))
		weston_layer_entry_insert(&w->floating_layer.view_list, &view->layer_link);
	weston_view_set_position(view, 200, 200);
}

bool workspace_move_floating_view(struct workspace *w, struct weston_view *view,
				  const struct weston_position *pos)
{
	struct weston_layer *layer = view->layer_link.layer;
	if (layer == &w->floating_layer) {
		weston_view_set_position(view, pos->x, pos->y);
		weston_view_schedule_repaint(view);
		return true;
	}
	return false;
}
