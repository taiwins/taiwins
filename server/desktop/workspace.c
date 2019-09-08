#include <string.h>
#include <stdlib.h>
#include "workspace.h"
#include "layout.h"
#include "../taiwins.h"


struct recent_view *
recent_view_create(struct weston_view *v, enum layout_type type)
{
	struct weston_desktop_surface *ds =
		weston_surface_get_desktop_surface(v->surface);
	struct recent_view *rv = zalloc(sizeof(struct recent_view));
	wl_list_init(&rv->link);
	rv->view = v;
	rv->type = type;
	//right now it should (0,0,0,0)
	rv->visible_geometry = weston_desktop_surface_get_geometry(ds);
	weston_desktop_surface_set_user_data(ds, rv);
	return rv;
}

void
recent_view_destroy(struct recent_view *rv)
{
	struct weston_desktop_surface *ds =
		weston_surface_get_desktop_surface(rv->view->surface);
	wl_list_remove(&rv->link);
	free(rv);
	weston_desktop_surface_set_user_data(ds, NULL);
}


/**
 * workspace implementation
 */
void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	weston_layer_init(&wp->tiling_layer, compositor);
	weston_layer_init(&wp->floating_layer, compositor);
	weston_layer_init(&wp->hidden_layer, compositor);
	weston_layer_init(&wp->fullscreen_layer, compositor);
	//init layout
	floating_layout_init(&wp->floating_layout, &wp->floating_layer);
	tiling_layout_init(&wp->tiling_layout, &wp->tiling_layer,
			   &wp->floating_layout);
	wl_list_init(&wp->recent_views);
	wp->current_layout = LAYOUT_TILING;
}

void
workspace_release(struct workspace *ws)
{
	struct weston_view *view, *next;
	struct weston_layer *layers[4]  = {
		&ws->floating_layer,
		&ws->tiling_layer,
		&ws->hidden_layer,
		&ws->fullscreen_layer,
	};
	//get rid of all the surface, maybe
	for (int i = 0; i < 4; i++) {
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
workspace_view_get_layout(const struct workspace *ws, const struct weston_view *v)
{
	if (!v || (v->layer_link.layer == &ws->fullscreen_layer) ||
	    (v->layer_link.layer == &ws->hidden_layer))
		return NULL;
	else if (v->layer_link.layer == &ws->floating_layer)
		return (struct layout *)&ws->floating_layout;
	else if (v->layer_link.layer == &ws->tiling_layer)
		return (struct layout *)&ws->tiling_layout;
	return NULL;
}

static void
apply_layout_operations(const struct layout_op *ops, const int len)
{
	for (int i = 0; i < len; i++) {
		if (ops[i].end)
			break;
		struct weston_desktop_surface *desk_surf =
			weston_surface_get_desktop_surface(ops[i].v->surface);
		struct recent_view *rv =
			weston_desktop_surface_get_user_data(desk_surf);
		weston_view_set_position(ops[i].v,
					 ops[i].pos.x - rv->visible_geometry.x,
					 ops[i].pos.y - rv->visible_geometry.y);
		if (ops[i].size.height && ops[i].size.width) {
			weston_desktop_surface_set_size(desk_surf, ops[i].size.width, ops[i].size.height);
			rv->visible_geometry.width = ops[i].size.width;
			rv->visible_geometry.height = ops[i].size.height;
		}
		weston_view_geometry_dirty(ops[i].v);
	}
}

static void
arrange_view_for_layout(struct workspace *ws, struct layout *layout,
			struct weston_view *v,
			const enum layout_command command,
			const struct layout_op *arg)
{
	//so this is the very smart part of the operation, you know the largest
	//possible number of operations, and give pass that into layouting
	//algorithm, so you don't need any memory allocations
	//here we have a extra buffer
	int len = wl_list_length(&ws->floating_layer.view_list.link) +
		wl_list_length(&ws->tiling_layer.view_list.link) +
		((command == DPSR_add) ? 2 : 1);
	struct layout_op ops[len];
	memset(ops, 0, sizeof(ops));
	layout->command(command, arg, v, layout, ops);
	apply_layout_operations(ops, len);
}

static void
arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
			const enum layout_command command,
			const struct layout_op *arg)
{
	//IF v is NULL, we need to re-arrange the entire output
	if (!v) {
		arrange_view_for_layout(ws, &ws->floating_layout, NULL,
					     command, arg);
		arrange_view_for_layout(ws, &ws->tiling_layout, NULL,
					     command, arg);
	} else {
		struct layout *layout = workspace_view_get_layout(ws, v);
		if (!layout)
			return;
		arrange_view_for_layout(ws, layout, v, command, arg);
	}
}


void
workspace_switch(struct workspace *to, struct workspace *from,
		 struct weston_keyboard *keyboard)
{
	weston_layer_unset_position(&from->floating_layer);
	weston_layer_unset_position(&from->tiling_layer);
	weston_layer_unset_position(&from->fullscreen_layer);

	weston_layer_set_position(&to->tiling_layer, WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&to->floating_layer , WESTON_LAYER_POSITION_NORMAL+1);
	weston_layer_set_position(&to->fullscreen_layer, WESTON_LAYER_POSITION_FULLSCREEN);
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
	if (!is_view_on_workspace(v, ws))
		return NULL;
	//view on floating layer, it is the easiest
	if (l == &ws->floating_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);
		//we should some how ping on it
	}  else if (l == &ws->tiling_layer) {
		//right now the implementation is moving all the views to hidden layer
		workspace_clear_floating(ws);
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->tiling_layer.view_list,
					  &v->layer_link);
	}
	else if (l == &ws->hidden_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);
	}
	else if (l == &ws->fullscreen_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->fullscreen_layer.view_list,
					  &v->layer_link);
	}
	//manage the recent views
	struct recent_view *rv = get_recent_view(v);
	wl_list_remove(&rv->link);
	wl_list_init(&rv->link);
	wl_list_insert(&ws->recent_views, &rv->link);

	weston_view_damage_below(v);
	weston_view_schedule_repaint(v);
	return true;
}

void
workspace_add_output(struct workspace *wp, struct taiwins_output *output)
{
	//for floating layout, we do need to do anything
	//TODO create the tiling_layout as well.
	layout_add_output(&wp->tiling_layout, output);
}

void
workspace_resize_output(struct workspace *wp, struct taiwins_output *output)
{
	layout_resize_output(&wp->tiling_layout, output);
	const struct layout_op arg = {
		.o = output->output,
	};
	arrange_view_for_workspace(wp, NULL, DPSR_output_resize, &arg);
}

void
workspace_remove_output(struct workspace *w, struct weston_output *output)
{
	layout_rm_output(&w->tiling_layout, output);
}

bool
is_view_on_workspace(const struct weston_view *v, const struct workspace *ws)
{
	if (!v || !ws)
		return false;
	const struct weston_layer *layer = v->layer_link.layer;
	return (layer == &ws->floating_layer || layer == &ws->tiling_layer ||
		layer == &ws->fullscreen_layer || layer == &ws->hidden_layer);

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
	struct layout_op arg = {
		.v = view,
		.default_geometry = {-1, -1, -1, -1},
	};
	struct recent_view *rv = get_recent_view(view);
	weston_layer_entry_remove(&view->layer_link);
	//TODO have switch case here to be able to add to different layer
	if (rv->type == LAYOUT_TILING)
		weston_layer_entry_insert(&w->tiling_layer.view_list,
					  &view->layer_link);
	else
		weston_layer_entry_insert(&w->floating_layer.view_list,
					  &view->layer_link);

	arrange_view_for_workspace(w, view, DPSR_add, &arg);

	workspace_focus_view(w, view);
}

static void
workspace_reinsert_view(struct workspace *w, struct weston_view *view)
{
	struct recent_view *rv = get_recent_view(view);
	struct layout_op arg = {
		.v = view,
		.default_geometry = rv->old_geometry,
	};
	weston_layer_entry_remove(&view->layer_link);
	if (rv->type == LAYOUT_TILING)
		weston_layer_entry_insert(&w->tiling_layer.view_list,
					  &view->layer_link);
	else
		weston_layer_entry_insert(&w->floating_layer.view_list,
					  &view->layer_link);
	arrange_view_for_workspace(w, view, DPSR_add, &arg);
	workspace_focus_view(w, view);
}

bool
workspace_remove_view(struct workspace *w, struct weston_view *view)
{
	if (!w || !view)
		return false;
	struct layout_op arg = {
		.v = view,
	};
	arrange_view_for_workspace(w, view, DPSR_del, &arg);
	weston_layer_entry_remove(&view->layer_link);
	wl_list_init(&view->layer_link.link);
	return true;
}

bool
workspace_move_view(struct workspace *w, struct weston_view *view,
		    const struct weston_position *pos)
{
	struct weston_layer *layer = view->layer_link.layer;
	struct weston_desktop_surface *dsurf =
		weston_surface_get_desktop_surface(view->surface);
	bool maximized = weston_desktop_surface_get_maximized(dsurf);
	bool fullscreened = weston_desktop_surface_get_fullscreen(dsurf);
	if (layer == &w->floating_layer &&
	    !maximized && !fullscreened ) {
		weston_view_set_position(view, pos->x, pos->y);
		weston_view_schedule_repaint(view);
		return true;
	}
	return false;
}

void
workspace_resize_view(struct workspace *w, struct weston_view *v,
		      wl_fixed_t x, wl_fixed_t y,
		      double dx, double dy)
{
	wl_fixed_t sx, sy;
	weston_view_from_global_fixed(v, x, y, &sx, &sy);

	struct layout_op arg = {
		.v = v,
		.dx = dx, .dy = dy,
		.sx = sx, .sy = sy,
	};
	arrange_view_for_workspace(w, v, DPSR_resize, &arg);
}

static void
workspace_fullmax_view(struct workspace *w, struct weston_view *v, bool max)
{
	struct layout_op ops[2];
	struct weston_output *output = v->output;
	struct weston_desktop_surface *dsurf =
		weston_surface_get_desktop_surface(v->surface);
	struct recent_view *rv =
		weston_desktop_surface_get_user_data(dsurf);
	if (max) {
		rv->old_geometry.x = v->geometry.x + rv->visible_geometry.x;
		rv->old_geometry.y = v->geometry.y + rv->visible_geometry.y;
		rv->old_geometry.width = rv->visible_geometry.width;
		rv->old_geometry.height = rv->visible_geometry.height;
	}
	ops[0].end = false;
	ops[0].v = v;
	ops[0].pos.x = (max) ? output->x : rv->old_geometry.x;
	ops[0].pos.y = (max) ? output->y : rv->old_geometry.y;
	ops[0].size.width = (max) ? output->width : rv->old_geometry.width;
	ops[0].size.height = (max) ? output->height : rv->old_geometry.height;
	ops[1].end = true;

	if (max) {
		apply_layout_operations(ops, 2);
	}
}

void
workspace_fullscreen_view(struct workspace *w, struct weston_view *v, bool fullscreen)
{
	weston_desktop_surface_set_fullscreen(
		weston_surface_get_desktop_surface(v->surface),
		fullscreen);
	workspace_fullmax_view(w, v, fullscreen);
	workspace_remove_view(w, v);
	if (fullscreen) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&w->fullscreen_layer.view_list,
					  &v->layer_link);
	} else
		workspace_reinsert_view(w, v);

}

void
workspace_maximize_view(struct workspace *w, struct weston_view *v, bool maximized)
{
	weston_desktop_surface_set_maximized(
		weston_surface_get_desktop_surface(v->surface), maximized);
	workspace_fullmax_view(w, v, maximized);
	if (!maximized) {
		workspace_remove_view(w, v);
		workspace_reinsert_view(w, v);
	}
}

void
workspace_minimize_view(struct workspace *w, struct weston_view *v)
{
	if (!is_view_on_workspace(v, w))
		return;
	workspace_remove_view(w, v);
	weston_layer_entry_remove(&v->layer_link);
	weston_layer_entry_insert(&w->hidden_layer.view_list,
				  &v->layer_link);
	//TODO: maybe focus on a view afterwards
}

void
workspace_view_run_command(struct workspace *w, struct weston_view *v,
			   enum layout_command command)
{
	struct layout_op arg = {
		.v = v,
	};
	arrange_view_for_workspace(w, v, command, &arg);
}

void
workspace_switch_layout(struct workspace *w, struct weston_view *view)
{
	struct weston_layer *layer = view->layer_link.layer;
	struct recent_view *rv = get_recent_view(view);
	if (layer != &w->floating_layer && layer != &w->tiling_layer)
		return;
	workspace_remove_view(w, view);
	rv->type = (rv->type == LAYOUT_TILING) ? LAYOUT_FLOATING : LAYOUT_TILING;
	/* rv->tiling = !rv->tiling; */
	workspace_add_view(w, view);
}

const char *
workspace_layout_name(struct workspace *ws)
{
	return (ws->current_layout == LAYOUT_TILING) ?
		"tiling" : "floating";
}
