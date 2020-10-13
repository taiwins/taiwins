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
#include <taiwins/objects/layers.h>
#include <wayland-server-protocol.h>

#include "layout.h"
#include "xdg.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct tw_xdg_output;
struct tw_workspace {
	struct tw_layers_manager *layers_manager;
	struct wl_list layouts;
	uint32_t idx;

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
	/**< "Planed size" are set by us, where clients need to resize to, but
	 * may not yet */
	uint32_t planed_w, planed_h, state;
	pixman_rectangle32_t old_geometry;
	/** added is set on view added to workspace, mapped is set on the first
	 * commit */
	bool mapped, added;

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

/**
 * @brief modify the view state to be focused or not.
 */
void
tw_xdg_view_set_focus(struct tw_xdg_view *view, bool focus);

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

bool
tw_workspace_has_view(const struct tw_workspace *ws,
                      const struct tw_xdg_view *v);
bool
tw_workspace_empty(const struct tw_workspace *ws);

/**
 * @brief move view to the front in the workspace.
 *
 * This action is different than tw_xdg_view_set_focus, which will only change
 * the view state.
 */
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
                         double dx, double dy,
                         enum wl_shell_surface_resize edge);
void
tw_workspace_run_command(struct tw_workspace *w,
                         enum tw_xdg_layout_command command,
                         struct tw_xdg_view *view);

//resize is done directly inside desktop for now
bool
tw_workspace_remove_view(struct tw_workspace *w, struct tw_xdg_view *v);

void
tw_workspace_fullscreen_view(struct tw_workspace *w, struct tw_xdg_view *v,
                             struct tw_xdg_output *output, bool fullscreen);
void
tw_workspace_maximize_view(struct tw_workspace *w, struct tw_xdg_view *v,
                           bool maximized);
void
tw_workspace_minimize_view(struct tw_workspace *w, struct tw_xdg_view *v);

void
tw_workspace_switch_layout(struct tw_workspace *w, enum tw_layout_type type);

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
