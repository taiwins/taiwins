#include <string.h>
#include <stdlib.h>
#include "workspace.h"
#include "layout.h"
#include "../taiwins.h"



size_t workspace_size =  sizeof(struct workspace);


/**
 * workspace implementation
 */
void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	weston_layer_init(&wp->tiling_layer, compositor);
	weston_layer_init(&wp->floating_layer, compositor);
	weston_layer_init(&wp->hidden_layer, compositor);
	//init layout
	floating_layout_init(&wp->floating_layout, &wp->floating_layer);
	tiling_layout_init(&wp->tiling_layout, &wp->tiling_layer,
			   &wp->floating_layout);
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
	floating_layout_end(&ws->floating_layout);
	tiling_layout_end(&ws->tiling_layout);
}


static inline struct weston_view *
workspace_get_top_view(const struct workspace *ws)
{
	struct weston_view *view;
	wl_list_for_each(view,
			 &ws->floating_layer.view_list.link, layer_link.link)
		return view;
	wl_list_for_each(view, &ws->tiling_layer.view_list.link, layer_link.link)
		return view;
	return NULL;
}

static struct layout *
workspace_get_layout_for_view(const struct workspace *ws, const struct weston_view *v)
{
	if (!v || (v->layer_link.layer != &ws->floating_layer &&
		   v->layer_link.layer != &ws->tiling_layer))
		return NULL;
	if (ws->floating_layout.layer == &ws->floating_layer)
		return (struct layout *)&ws->floating_layout;
	return NULL;
}

void
arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
			const enum layout_command command,
			const struct layout_op *arg)
{
	struct layout *layout = workspace_get_layout_for_view(ws, v);
	if (!layout)
		return;
	//so this is the very smart part of the operation, you know the largest
	//possible number of operations, and give pass that into layouting
	//algorithm, so you don't need any memory allocations

	//this should be max(2, ws->tiling_layer.view_list)
	int len = wl_list_length(&ws->floating_layer.view_list.link) +
		wl_list_length(&ws->tiling_layer.view_list.link) + 1;
	struct layout_op ops[len];
	memset(ops, 0, sizeof(ops));
	layout->command(command, arg, v, layout, ops);
	for (int i = 0; i < len; i++) {
		if (ops[i].end)
			break;
		//TODO, check the validty of the operations
		weston_view_set_position(v, ops[i].pos.x, ops[i].pos.y);
		weston_view_schedule_repaint(v);
	}
}


void
workspace_switch(struct workspace *to, struct workspace *from,
		 struct weston_keyboard *keyboard)
{
	weston_layer_unset_position(&from->floating_layer);
	weston_layer_unset_position(&from->tiling_layer);

	weston_layer_set_position(&to->tiling_layer, WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&to->floating_layer , WESTON_LAYER_POSITION_NORMAL+1);
	struct weston_view *view = workspace_get_top_view(to);

	if (!keyboard)
		return;
	if (keyboard->focus)
		if (keyboard->focus)
			tw_lose_surface_focus(keyboard->focus);
	if (view)
		weston_keyboard_set_focus(keyboard, view->surface);

	weston_compositor_damage_all(to->floating_layer.compositor);
	weston_compositor_schedule_repaint(to->floating_layer.compositor);
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
	//for floating layout, we do need to do anything
	//TODO create the tiling_layout as well.
	layout_add_output(&wp->tiling_layout, output);
}

void
workspace_remove_output(struct workspace *w, struct weston_output *output)
{
	layout_rm_output(&w->tiling_layout, output);
}

bool
is_view_on_workspace(const struct weston_view *v, const struct workspace *ws)
{
	const struct weston_layer *layer = v->layer_link.layer;
	return (layer == &ws->floating_layer || layer == &ws->tiling_layer);

}

bool
is_workspace_empty(const struct workspace *ws)
{
	return wl_list_empty(&ws->tiling_layer.view_list.link) &&
		wl_list_empty(&ws->floating_layer.view_list.link) &&
		wl_list_empty(&ws->hidden_layer.view_list.link);
}


void
workspace_add_view(struct workspace *w, struct weston_view *view)
{
	if (wl_list_empty(&view->layer_link.link))
		weston_layer_entry_insert(&w->floating_layer.view_list, &view->layer_link);
	struct layout_op arg = {
		.v = view,
	};
	arrange_view_for_workspace(w, view, DPSR_add, &arg);
}

bool workspace_move_view(struct workspace *w, struct weston_view *view,
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
