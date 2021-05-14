/*
 * shell_internal.h - taiwins shell internal header
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

#ifndef TW_SHELL_INTERNAL_H
#define TW_SHELL_INTERNAL_H

#include <pixman.h>
#include <wayland-util.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/popup_grab.h>
#include <taiwins/shell.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_shell_ui_anchor_pos {
	TW_SHELL_UI_ANCHOR_TOP = 1,
	TW_SHELL_UI_ANCHOR_BOTTOM = 2,
	TW_SHELL_UI_ANCHOR_LEFT = 4,
	TW_SHELL_UI_ANCHOR_RIGHT = 8,
};

struct tw_shell;
struct tw_shell_output;

struct tw_shell_ui {
	struct tw_shell *shell;
	struct tw_shell_output *output;
	struct wl_resource *resource;
	struct tw_surface *binded;

	int32_t x, y;
	struct tw_layer *layer;
	enum taiwins_ui_type type;
	struct wl_listener surface_destroy;
	struct wl_listener grab_close;
	struct tw_popup_grab grab;
	//I need to translate this into size, positional.
	struct {
		uint32_t x, y, w, h;
		uint32_t anchor, occlusion_zone;
		pixman_box32_t margin;
		struct tw_layer *layer;
	} pending;
};

/**
 * @brief represents tw_output
 *
 * the resource only creates for taiwins_shell object
 */
struct tw_shell_output {
	struct tw_engine_output *output;
	struct tw_shell *shell;
	struct wl_list link; /**< tw_shell:heads */
	//ui elems
	uint32_t id;
	int32_t panel_height;
	struct tw_shell_ui background;
	struct tw_shell_ui panel;
};

struct tw_shell {
	uid_t uid; gid_t gid; pid_t pid;
	char path[256];

        struct wl_display *display;
	struct wl_client *shell_client;
	struct wl_resource *shell_resource;
	struct wl_global *shell_global;
	struct wl_global *layer_shell;
	struct tw_engine *engine;

	struct tw_layer background_layer;
	struct tw_layer bottom_ui_layer;
	struct tw_layer ui_layer;
	struct tw_layer locker_layer;

	struct tw_surface *the_widget_surface;
	enum taiwins_shell_panel_pos panel_pos;

	struct wl_signal desktop_area_signal;
	struct wl_signal widget_create_signal;
	struct wl_signal widget_close_signal;

        struct wl_listener display_destroy_listener;
        struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener output_resize_listener;
	struct tw_subprocess process;

	struct tw_shell_ui widget;
	struct tw_shell_ui locker;
	bool ready;
	//we deal with at most 16 outputs
	struct wl_list heads; /**< tw_shell_output:link */
	struct tw_shell_output tw_outputs[16];
};

bool
shell_impl_layer_shell(struct tw_shell *shell, struct wl_display *display);

struct tw_shell_ui *
shell_create_ui_element(struct tw_shell *shell,
                        struct tw_shell_ui *elem,
                        struct wl_resource *ui_resource,
                        struct tw_surface *surface,
                        struct tw_shell_output *output,
                        uint32_t x, uint32_t y,
                        struct tw_layer *layer,
                        const struct tw_surface_role *role);
void
shell_ui_set_role(struct tw_shell_ui *ui, const struct tw_surface_role *role,
                  struct tw_surface *surface);

struct tw_shell_output *
shell_output_from_engine_output(struct tw_shell *shell,
                                struct tw_engine_output *output);
#ifdef  __cplusplus
}
#endif

#endif /* EOF */
