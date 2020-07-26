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

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <objects/layers.h>

#include "backend/backend.h"
#include "layout.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct tw_xdg_output;
struct tw_workspace {
	struct tw_layers_manager *layers_manager;
	// what if we have different layout?
	//TODO: replace with a list of layouts.
	/* struct tw_xdg_layout floating_layout; */
	/* struct tw_xdg_layout tiling_layout; */
	struct wl_list layouts;
	uint32_t idx;

	// workspace does not distinguish the outputs.
	// so when we `switch_workspace, all the output has to update.
	// The layouting algorithm may have to worry about output
        struct tw_layer hidden_layer;
	/* the layers reflects the layer positions, fullscreen application has
	 * to stay on top of UI layer thus requires additional layers */
	struct tw_layer fullscreen_back_layer;
	struct tw_layer back_layer;
        /* here we have the tiling views, because tiling views have to occupy
         * the whole output, so it has to have its own layer.
         */
	struct tw_layer mid_layer;
	struct tw_layer front_layer;
	struct tw_layer fullscreen_layer;

	/** current workspace can be in state like floating, tiling,
	 * fullscreen */
	enum tw_layout_type current_layout;

	// The list will be used in creating/deleting views. switch workspace,
	// switch views by key, click views
	struct wl_list recent_views;

	// the only tiling layer here will create the problem when we want to do
	// the stacking layout, for example. Only show two views.
};


/**
 * @brief xdg_view, represents a mapped desktop surface
 */
struct tw_xdg_view {
	struct tw_desktop_surface *dsurf;
	struct wl_signal dsurf_umapped_signal;
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
	int32_t x, y;
	pixman_rectangle32_t old_geometry;
	bool mapped;

	struct wl_list link;
	enum tw_layout_type type, prev_type;
	struct tw_xdg_layout *layout;
	struct tw_layer *layer;
	struct tw_xdg_output *output;

	struct {
		int32_t x;
		int32_t y;
		bool is_xwayland;
	} xwayland;
};

struct tw_xdg_view *
tw_xdg_view_create(struct tw_desktop_surface *dsurf);

void
tw_xdg_view_destroy(struct tw_xdg_view *view);

void
tw_xdg_view_set_position(struct tw_xdg_view *view, int x, int y);

void
tw_xdg_view_configure_size(struct tw_xdg_view *view, uint32_t w, uint32_t h);

/******************************************************************************
 * workspace API
 *****************************************************************************/
void
tw_workspace_init(struct tw_workspace *wp, struct tw_layers_manager *layers,
                  uint32_t idx);

void
tw_workspace_release(struct tw_workspace *);

struct tw_xdg_view *
tw_workspace_switch(struct tw_workspace *to, struct tw_workspace *from);

struct tw_xdg_view *
tw_workspace_get_top_view(const struct tw_workspace *ws);

const char *
tw_workspace_layout_name(struct tw_workspace *ws);

bool
tw_workspace_has_view(const struct tw_workspace *ws,
                      const struct tw_xdg_view *v);
bool
tw_workspace_empty(const struct tw_workspace *ws);

//we probably should leave this function to arrange_view_for_workspace
bool
tw_workspace_focus_view(struct tw_workspace *ws, struct tw_xdg_view *v);

struct tw_xdg_view *
tw_workspace_defocus_view(struct tw_workspace *ws,
                          struct tw_xdg_view *v);
void
tw_workspace_add_view(struct tw_workspace *w, struct tw_xdg_view *v);

bool
tw_workspace_move_view(struct tw_workspace *w, struct tw_xdg_view *v,
                       double dx, double dy);
void
tw_workspace_resize_view(struct tw_workspace *w, struct tw_xdg_view *v,
                         double dx, double dy);
void
tw_workspace_run_layout_command(struct tw_workspace *w,
                                enum tw_xdg_layout_command command,
                                const struct tw_xdg_layout_op *op);

//resize is done directly inside desktop for now
bool
tw_workspace_remove_view(struct tw_workspace *w, struct tw_xdg_view *v);

void
tw_workspace_fullscreen_view(struct tw_workspace *w, struct tw_xdg_view *v,
                             struct tw_xdg_output *output, bool fullscreen);
void
tw_workspace_maximize_view(struct tw_workspace *w, struct tw_xdg_view *v,
                           pixman_rectangle32_t *geo, bool maximized);
void
tw_workspace_minimize_view(struct tw_workspace *w, struct tw_xdg_view *v);

void
tw_workspace_switch_layout(struct tw_workspace *w, struct tw_xdg_view *v);

void
tw_workspace_add_output(struct tw_workspace *wp,
                        struct tw_xdg_output *output);
void
tw_workspace_remove_output(struct tw_workspace *w,
                           struct tw_xdg_output *output);
void
tw_workspace_resize_output(struct tw_workspace *wp,
                           struct tw_xdg_output *output);

#ifdef  __cplusplus
}
#endif



#endif
