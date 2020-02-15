/*
 * shell.h - taiwins client shell header
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

#ifndef TW_SHELL_CLIENT_H
#define TW_SHRLL_CLIENT_H

#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <cairo/cairo.h>
#include <poll.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include <os/file.h>
#include <client.h>
#include <egl.h>
#include <shmpool.h>
#include <nk_backends.h>
#include "../shared_config.h"
#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	//options
	struct {
		struct tw_bbox bbox;
		int index;
	};
	struct tw_ui *bg_ui;
	struct tw_ui *pn_ui;
	struct tw_appsurf background;
	struct tw_appsurf panel;
	struct tw_app_event_filter background_events;
	//a temporary struct
	double widgets_span;
};

//state of current widget and widget to launch
struct widget_launch_info {
	uint32_t x;
	uint32_t y;
	struct shell_widget *widget;
	struct shell_widget *current;
};

struct desktop_shell {
	struct tw_globals globals;
	struct tw_shell *interface;
	enum tw_shell_panel_pos panel_pos;
	//pannel configuration
	struct {
		struct nk_wl_backend *panel_backend;
		struct nk_style_button label_style;
		struct nk_user_font *icon_font;
		char wallpaper_path[128];
		//TODO calculated from font size
		size_t panel_height;
	};
	//widget configures
	struct {
		struct nk_wl_backend *widget_backend;
		struct wl_list shell_widgets;
		struct widget_launch_info widget_launch;
		//surface like locker, on-screen-keyboard will use this surface.
		struct tw_ui *transient_ui;
		struct tw_appsurf transient;
	};
	//outputs
	struct shell_output *main_output;
	struct shell_output shell_outputs[16];


};

static inline int
desktop_shell_n_outputs(struct desktop_shell *shell)
{
	for (int i = 0; i < 16; i++)
		if (shell->shell_outputs[i].shell == NULL)
			return i;
	return 16;
}


void shell_init_bg_for_output(struct shell_output *output);
void shell_resize_bg_for_output(struct shell_output *output);

void shell_init_panel_for_output(struct shell_output *output);
void shell_resize_panel_for_output(struct shell_output *output);

void shell_locker_init(struct desktop_shell *shell);

void shell_process_msg(struct desktop_shell *shell, uint32_t type,
		       const struct wl_array *data);

static inline void
shell_end_transient_surface(struct desktop_shell *shell)
{
	tw_ui_destroy(shell->transient_ui);
	shell->transient_ui = NULL;
	tw_appsurf_release(&shell->transient);
}

#ifdef __cplusplus
}
#endif


#endif /* EOF */
