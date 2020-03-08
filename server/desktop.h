/*
 * desktop.h - taiwins desktop header
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

#include <wayland-server.h>
#include <sequential.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include "bindings.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1

struct shell;
struct desktop;
struct console;
struct tw_config;
struct shell_ui;


/**
 * @brief annouce globals
 */
struct shell *announce_shell(struct weston_compositor *compositor, const char *path,
			     struct tw_config *config);

void announce_console(struct weston_compositor *compositor,
				 struct shell *shell, const char *exec_path,
				 struct tw_config *config);

void announce_desktop(struct weston_compositor *compositor,
				 struct shell *shell,
				 struct tw_config *config);

void annouce_theme(struct weston_compositor *ec, struct shell *shell,
		   struct tw_config *config);

// other APIs

struct wl_client *shell_get_client(struct shell *shell);

void shell_create_ui_elem(struct shell *shell, struct wl_client *client,
			  uint32_t tw_ui, struct wl_resource *wl_surface,
			  uint32_t x, uint32_t y, enum taiwins_ui_type type);

void shell_post_data(struct shell *shell, uint32_t type, struct wl_array *msg);
void shell_post_message(struct shell *shell, uint32_t type, const char *msg);

struct weston_geometry
shell_output_available_space(struct shell *shell, struct weston_output *weston_output);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
