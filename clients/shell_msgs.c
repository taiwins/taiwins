/*
 * shell_msg.c - taiwins client shell msg implementation
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

#include <strops.h>
#include "shell.h"

static inline void
kill_widget(struct desktop_shell *shell)
{
	tw_ui_destroy(shell->widget_launch.current->proxy);
	shell->widget_launch.current->proxy = NULL;
	app_surface_release(&shell->widget_launch.current->widget);
	shell->widget_launch.current = NULL;
}

static vector_t
taiwins_menu_from_wl_array(const struct wl_array *serialized)
{
	vector_t dst;
	vector_t src;
	vector_init_zero(&dst, sizeof(struct taiwins_menu_item), NULL);
	src = dst;
	src.alloc_len = serialized->size / (sizeof(struct taiwins_menu_item));
	src.len = src.alloc_len;
	src.elems = serialized->data;
	vector_copy(&dst, &src);
	return dst;
}

static void
desktop_shell_setup_menu(struct desktop_shell *shell,
			 const struct wl_array *serialized)
{
	vector_t menus = taiwins_menu_from_wl_array(serialized);
	//what you do here? you can validate it, then copy to shell.
	vector_destroy(&menus);
}

static void
desktop_shell_setup_wallpaper(struct desktop_shell *shell, const char *path)
{
	if (is_file_exist(path))
		strop_ncpy(shell->wallpaper_path, path, 128);
	for (int i = 0; i < desktop_shell_n_outputs(shell); i++) {
		struct app_surface *bg =
			&shell->shell_outputs[i].background;
		if (bg->wl_surface)
			app_surface_frame(bg, false);
	}
}

static void
desktop_shell_setup_widgets(struct desktop_shell *shell, const char *path)
{
	if (is_file_exist(path)) {
		shell_widgets_load_script(&shell->shell_widgets,
					  &shell->globals.event_queue,
					  path);
	}
	if (shell->main_output) {
		struct shell_widget *widget;
		struct app_surface *panel = &shell->main_output->panel;
		wl_list_for_each(widget, &shell->shell_widgets, link) {
			shell_widget_hook_panel(widget, panel);
			shell_widget_activate(widget, &shell->globals.event_queue);
		}
	}
}

static void
desktop_shell_setup_locker(struct desktop_shell *shell)
{
	//priority over another
	if (shell->transient.wl_surface &&
	    shell->transient.type == APP_SURFACE_LOCKER)
		return;
	else if (shell->transient.wl_surface)
		shell_end_transient_surface(shell);
	if (shell->widget_launch.current)
		kill_widget(shell);

	shell_locker_init(shell);
	app_surface_frame(&shell->transient, false);
}

void
shell_process_msg(struct desktop_shell *shell,
		  uint32_t type, const struct wl_array *data)
{
	union wl_argument arg;

	switch (type) {
	case TW_SHELL_MSG_TYPE_NOTIFICATION:
		arg.s = data->data;
		break;
	case TW_SHELL_MSG_TYPE_PANEL_POS:
		arg.u = atoi((const char*)data->data);
		shell->panel_pos = arg.u == TW_SHELL_PANEL_POS_TOP ?
			TW_SHELL_PANEL_POS_TOP : TW_SHELL_PANEL_POS_BOTTOM;
		break;
	case TW_SHELL_MSG_TYPE_MENU:
		desktop_shell_setup_menu(shell, data);
		break;
	case TW_SHELL_MSG_TYPE_WALLPAPER:
		desktop_shell_setup_wallpaper(shell, (const char *)data->data);
		break;
	case TW_SHELL_MSG_TYPE_WIDGET:
		desktop_shell_setup_widgets(shell, (const char *)data->data);
		break;
	case TW_SHELL_MSG_TYPE_LOCK:
		desktop_shell_setup_locker(shell);
		break;
	case TW_SHELL_MSG_TYPE_SWITCH_WORKSPACE:
	{
		fprintf(stderr, "switch workspace\n");
		break;
	}
	default:
		break;
	}

}
