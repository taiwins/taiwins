/*
 * workspace.h - taiwins desktop workspace header
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef TW_WORKSPACE_H
#define TW_WORKSPACE_H

#include <libweston/libweston.h>
#include <libweston-desktop/libweston-desktop.h>
#include <stdbool.h>
#include <unistd.h>

#include "../taiwins.h"
#include "layout.h"

#define FRONT_LAYER_POS WESTON_LAYER_POSITION_NORMAL+1
#define BACK_LAYER_POS  WESTON_LAYER_POSITION_NORMAL

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
	enum tw_layout_type current_layout;

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
	struct weston_geometry old_geometry;
	struct wl_list link;
	enum tw_layout_type type;

	struct {
		int32_t x;
		int32_t y;
		bool is_xwayland;
	} xwayland;
};

/*************************************************************
 * recent views
 ************************************************************/
struct recent_view *recent_view_create(struct weston_view *view, enum tw_layout_type layout);
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


/************************************************************
 * workspace API
 ***********************************************************/
void
workspace_init(struct workspace *wp, struct weston_compositor *compositor);

void
workspace_release(struct workspace *);

struct weston_view *
workspace_switch(struct workspace *to, struct workspace *from);

struct weston_view *
workspace_get_top_view(const struct workspace *ws);

const char *
workspace_layout_name(struct workspace *ws);

bool
is_view_on_workspace(const struct weston_view *v, const struct workspace *ws);

bool
is_workspace_empty(const struct workspace *ws);

//we probably should leave this function to arrange_view_for_workspace
bool
workspace_focus_view(struct workspace *ws, struct weston_view *v);

struct weston_view *
workspace_defocus_view(struct workspace *ws, struct weston_view *v);

void
workspace_add_view(struct workspace *w, struct weston_view *view);

bool
workspace_move_view(struct workspace *w, struct weston_view *v,
                    const struct weston_position *pos);
void
workspace_resize_view(struct workspace *w, struct weston_view *v,
                      wl_fixed_t x, wl_fixed_t y,
                      double dx, double dy);

void
workspace_view_run_command(struct workspace *w, struct weston_view *v,
                           enum layout_command command);

//resize is done directly inside desktop for now
bool
workspace_remove_view(struct workspace *w, struct weston_view *v);

void
workspace_fullscreen_view(struct workspace *w, struct weston_view *v,
                          bool fullscreen);

void
workspace_maximize_view(struct workspace *w, struct weston_view *v,
                        bool maximized, const struct weston_geometry *geo);

void
workspace_minimize_view(struct workspace *w, struct weston_view *v);

void
workspace_switch_layout(struct workspace *w, struct weston_view *v);

void
workspace_add_output(struct workspace *wp, struct tw_output *output);

void
workspace_remove_output(struct workspace *w, struct weston_output *output);

void
workspace_resize_output(struct workspace *wp, struct tw_output *output);


#ifdef  __cplusplus
}
#endif



#endif
