/*
 * xdg.h - taiwins desktop shell header
 *
 * Copyright (c) 2020 Xichen Zhou
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

#ifndef TW_XDG_H
#define TW_XDG_H

#include <pixman.h>
#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#include <taiwins/objects/seat.h>
#include <taiwins/objects/desktop.h>
#include "backend.h"

#define MAX_WORKSPACES 9

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @brief taiwins output information
 *
 * here we define some template structures. It is passed as pure data, and they
 * are not persistent. So don't store them as pointers.
 */
struct tw_xdg_output {
	struct tw_backend_output *output;
	pixman_rectangle32_t desktop_area;
	int32_t idx;
	uint32_t inner_gap;
	uint32_t outer_gap;
};

//not sure if we want to make it here
enum tw_xdg_view_resize_option {
	RESIZE_LEFT, RESIZE_RIGHT,
	RESIZE_UP, RESIZE_DOWN,
};

enum tw_layout_type {
	LAYOUT_FLOATING,
	LAYOUT_TILING,
	LAYOUT_MAXIMIZED,
	LAYOUT_FULLSCREEN,
};

/******************************************************************************
 * desktop functions
 *****************************************************************************/
struct tw_shell;
struct tw_xdg;
struct tw_xdg_view;

struct tw_xdg *
tw_xdg_create_global(struct wl_display *display, struct tw_shell *shell,
                     struct tw_backend *backend);
int
tw_xdg_current_workspace_idx(struct tw_xdg *xdg);

int
tw_xdg_last_workspace_idx(struct tw_xdg *xdg);

void
tw_xdg_switch_workspace(struct tw_xdg *xdg, uint32_t to);

/**
 * @brief getting a xdg surface from tw_surface
 */
struct tw_xdg_view *
tw_xdg_view_from_tw_surface(struct tw_surface *surface);

void
tw_xdg_view_activate(struct tw_xdg *xdg, struct tw_xdg_view *view);

void
tw_xdg_toggle_view_split(struct tw_xdg *xdg, struct tw_xdg_view *view);

void
tw_xdg_toggle_view_layout(struct tw_xdg *xdg, struct tw_xdg_view *view);

void
tw_xdg_split_on_view(struct tw_xdg *xdg, struct tw_xdg_view *view,
                     bool vsplit);
void
tw_xdg_merge_view(struct tw_xdg *xdg, struct tw_xdg_view *view);

void
tw_xdg_resize_view(struct tw_xdg *xdg, struct tw_xdg_view *view,
                   int32_t dx, int32_t dy, enum wl_shell_surface_resize edge);
bool
tw_xdg_start_moving_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                         struct tw_seat *seat);
bool
tw_xdg_start_resizing_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                           enum wl_shell_surface_resize edge,
                           struct tw_seat *seat);
bool
tw_xdg_start_task_switching_grab(struct tw_xdg *xdg, uint32_t time,
                                 uint32_t key,  uint32_t modifiers_state,
                                 struct tw_seat *seat);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
