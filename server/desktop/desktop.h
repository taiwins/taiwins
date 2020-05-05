/*
 * shell.h - taiwins desktop shell header
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

#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include <stdint.h>
#include <stdbool.h>
#include <libweston/libweston.h>
#include "../taiwins.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @brief taiwins output information
 *
 * here we define some template structures. It is passed as pure data, and they
 * are not persistent. So don't store them as pointers.
 */
struct tw_output {
	struct weston_output *output;
	//available space used in desktop area. We don't have the configureation
	//code yet, once it is available, it can be used to create this struct.
	struct weston_geometry desktop_area;
	uint32_t inner_gap;
	uint32_t outer_gap;
};

//not sure if we want to make it here
enum tw_desktop_view_resize_option {
	RESIZE_LEFT, RESIZE_RIGHT,
	RESIZE_UP, RESIZE_DOWN,
};

/*******************************************************************************
 * desktop functions
 ******************************************************************************/
struct desktop;
struct shell;

struct desktop *
tw_setup_desktop(struct weston_compositor *compositor,
                 struct shell *shell);

struct desktop *tw_desktop_get_global();

int
tw_desktop_num_workspaces(struct desktop *desktop);

int
tw_desktop_get_current_workspace(struct desktop *desktop);

int
tw_desktop_get_last_workspace(struct desktop *desktop);

const char *
tw_desktop_get_workspace_layout(struct desktop *desktop, unsigned int i);

bool
tw_desktop_set_workspace_layout(struct desktop *desktop, unsigned int i,
                                enum tw_layout_type type);
void
tw_desktop_get_gap(struct desktop *desktop, int *inner, int *outer);

void
tw_desktop_set_gap(struct desktop *desktop, int inner, int outer);

void
tw_desktop_start_transparency_grab(struct desktop *desktop,
                                   struct weston_pointer *pointer);
void
tw_desktop_start_moving_grab(struct desktop *desktop,
                             struct weston_pointer *pointer);
void
tw_desktop_start_task_switch_grab(struct desktop *desktop,
                                  struct weston_keyboard *keyboard);
bool
tw_desktop_activate_view(struct desktop *desktop, struct weston_view *view);

struct weston_view *
tw_desktop_switch_workspace(struct desktop *desktop, uint32_t to);

void
tw_desktop_start_resize_grab(struct desktop *desktop, struct weston_view *view,
                             uint32_t option);
void
tw_desktop_toggle_view_layout(struct desktop *desktop,
                              struct weston_view *view);
void
tw_desktop_toggle_view_split(struct desktop *desktop, struct weston_view *view);

void
tw_desktop_split_on_view(struct desktop *desktop, struct weston_view *view,
                         bool vsplit);
void
tw_desktop_merge_view(struct desktop *desktop, struct weston_view *view);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
