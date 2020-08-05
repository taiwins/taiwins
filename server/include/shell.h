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

#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <pixman.h>
#include <wayland-server.h>
#include <ctypes/sequential.h>
#include <wayland-taiwins-shell-server-protocol.h>

#include "backend.h"

#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_shell;
struct tw_console;
struct tw_theme;

struct tw_shell *
tw_shell_create_global(struct wl_display *display, struct tw_backend *backend,
                       bool enable_layer_shell, const char *path);
void
tw_shell_create_ui_elem(struct tw_shell *shell, struct wl_client *client,
                        struct tw_backend_output *output,
                        uint32_t tw_ui, struct wl_resource *wl_surface,
                        uint32_t x, uint32_t y, enum taiwins_ui_type type);
void
tw_shell_post_data(struct tw_shell *shell, uint32_t type,
                   struct wl_array *msg);
void
tw_shell_post_message(struct tw_shell *shell, uint32_t type, const char *msg);

pixman_rectangle32_t
tw_shell_output_available_space(struct tw_shell *shell,
                                struct tw_backend_output *output);
struct wl_signal *
tw_shell_get_desktop_area_signal(struct tw_shell *shell);

void
tw_shell_set_panel_pos(struct tw_shell *shell,
                       enum taiwins_shell_panel_pos pos);

struct tw_theme *
tw_theme_create_global(struct wl_display *display);

struct tw_console *
tw_console_create_global(struct wl_display *display, const char *path,
                         struct tw_backend *backend, struct tw_shell *shell);
void
tw_console_start_client(struct tw_console *console);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
