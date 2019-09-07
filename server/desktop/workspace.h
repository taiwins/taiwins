#ifndef TW_WORKSPACE_H
#define TW_WORKSPACE_H

#include <stdbool.h>
#define INCLUDE_DESKTOP
#include "../taiwins.h"
#include "unistd.h"
#include "layout.h"

#ifdef  __cplusplus
extern "C" {
#endif



struct workspace {
	struct layout floating_layout;
	struct layout tiling_layout;
	//workspace does not distinguish the outputs.
	//so when we `switch_workspace, all the output has to update.
	//The layouting algorithm may have to worry about output
	struct weston_layer hidden_layer;
	struct weston_layer tiling_layer;
	struct weston_layer floating_layer;
	struct weston_layer fullscreen_layer;

	/** current workspace can be in state like floating, tiling, fullscreen */
	enum layout_type current_layout;

	//this list will be used in creating/deleting views. switch workspace,
	//switch views by key, click views will be horrible though. You have to
	//go through the list
	//what about a hashed link-list ? Will it be faster?
	struct wl_list recent_views;

	//the only tiling layer here will create the problem when we want to do
	//the stacking layout, for example. Only show two views.
};

struct recent_view {
	struct weston_view *view;
	/*
	  desktop surface has decorations(invisible portion)
	  -----------------------
	  |   x                 | --> x,y : visible point
	  | y ----------------  |
	  |   |              |  |
	  |   |    visible   |  |
	  |   |              |  |
	  |   ---------------- w|
	  |                   h |
	  -----------------------

	  view postion should be x-visible_geometry.x, y-visible_geometry.y
	  x: visible geometry starts at x.
	  y: decoration lenght in y.
	*/

	struct weston_geometry visible_geometry;
	struct wl_list link;
	enum layout_type type;
};

struct recent_view *recent_view_create(struct weston_view *view, enum layout_type layout);
void recent_view_destroy(struct recent_view *);

static inline struct recent_view *
get_recent_view(struct weston_view *v)
{
	struct weston_desktop_surface *desk_surf =
		weston_surface_get_desktop_surface(v->surface);
	struct recent_view *rv =
		weston_desktop_surface_get_user_data(desk_surf);
	return rv;
}



static inline void
recent_view_get_origin_coord(const struct recent_view *v, float *x, float *y)
{
	*x = v->view->geometry.x + v->visible_geometry.x;
	*y = v->view->geometry.y + v->visible_geometry.y;
}

extern size_t workspace_size;

void workspace_init(struct workspace *wp, struct weston_compositor *compositor);
void workspace_release(struct workspace *);
void workspace_switch(struct workspace *to, struct workspace *from,
		      struct weston_keyboard *keyboard);

const char *workspace_layout_name(struct workspace *ws);

void arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
				const enum layout_command command,
				const struct layout_op *arg);

bool is_view_on_workspace(const struct weston_view *v, const struct workspace *ws);
bool is_workspace_empty(const struct workspace *ws);

//we probably should leave this function to arrange_view_for_workspace
bool workspace_focus_view(struct workspace *ws, struct weston_view *v);
void workspace_add_view(struct workspace *w, struct weston_view *view);
bool workspace_move_view(struct workspace *w, struct weston_view *v,
				  const struct weston_position *pos);
bool workspace_remove_view(struct workspace *w, struct weston_view *v);

void workspace_switch_layout(struct workspace *w, struct weston_view *v);


void workspace_add_output(struct workspace *wp, struct taiwins_output *output);
void workspace_remove_output(struct workspace *w, struct weston_output *output);
void workspace_resize_output(struct workspace *wp, struct taiwins_output *output);


#ifdef  __cplusplus
}
#endif



#endif
