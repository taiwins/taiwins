/*
 * config_bindings.c - taiwins config bindings implementation
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

#include <assert.h>
#include <ctypes/helpers.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/logger.h>
#include <taiwins/shell.h>
#include <wayland-server.h>

#include "bindings.h"
#include "config.h"

static bool
quit_compositor(struct tw_keyboard *keyboard, uint32_t time,
                uint32_t key, uint32_t modifiers, uint32_t option, void *data)
{
	struct tw_config *config = data;
	struct wl_display *wl_display = config->engine->display;

	tw_logl("%s: quit taiwins", "taiwins");
	wl_display_terminate(wl_display);
	return true;
}

/* TW_CLOSE_APP_BINDING */
static bool
close_application(struct tw_keyboard *keyboard, uint32_t time,
                  uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct wl_resource *resource = keyboard->focused_surface;
	struct tw_surface *surface = (keyboard->focused_surface) ?
		tw_surface_from_resource(resource) : NULL;

	if (surface && tw_desktop_surface_from_tw_surface(surface))
		wl_client_destroy(wl_resource_get_client(resource));
	return true;
}

/* TW_RELOAD_CONFIG_BINDING */
static bool
reload_config(struct tw_keyboard *keyboard, uint32_t time,
              uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_config *config = data;
	struct tw_shell *shell = tw_config_request_object(config, "shell");
	char *err_msg = NULL;

	if (!tw_config_run(config, &err_msg)) {
		const char *send_msg = err_msg ?
			err_msg : "Config failed with Unknown reason";

		tw_shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_CONFIG_ERR,
		                      send_msg);
		if (err_msg)
			free(err_msg);
	}
	return true;
}

/* TW_OPEN_CONSOLE_BINDING */
static bool
should_start_console(struct tw_keyboard *keyboard, uint32_t time,
                     uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_config *config = data;
	struct tw_console *console =
		tw_config_request_object(config, "console");
	tw_console_start_client(console);
	return true;
}

/* TW_ZOOM_AXIS_BINDING */
static bool
zoom_axis(struct tw_pointer *pointer, uint32_t time, double delta,
          enum wl_pointer_axis direction, uint32_t modifiers, void *data)
{
	return true;
}

/* TW_ALPHA_AXIS_BINDING */
static bool
alpha_axis(struct tw_pointer *pointer, uint32_t time, double delta,
           enum wl_pointer_axis direction, uint32_t modifiers, void *data)
{
	//TODO: do we operate on tw_surface or a tw_xdg_view?
	/* struct tw_config *config = data; */
	/* struct tw_xdg *desktop = */
	/*	tw_config_request_object(config, "desktop"); */

	if (!pointer->focused_surface)
		return false;
	return true;
	/* tw_desktop_start_transparency_grab(desktop, pointer); */
}

static bool
moving_surface_pointer(struct tw_pointer *pointer, const uint32_t time,
                       uint32_t button, uint32_t modifiers, void *data)
{
	struct tw_xdg_view *view;
	struct tw_surface *surface;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");
	struct tw_seat *seat = container_of(pointer, struct tw_seat, pointer);
	if (desktop && pointer->focused_surface) {
		surface = tw_surface_from_resource(pointer->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (pointer->btn_count > 0 && view) {
			tw_xdg_start_moving_grab(desktop, view, seat);
		}
	}
	return true;
}

static bool
click_activate_surface(struct tw_pointer *pointer, uint32_t time,
                       uint32_t button, uint32_t modifiers, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");

	if (desktop && pointer->focused_surface) {
		surface = tw_surface_from_resource(pointer->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);

		if (pointer->btn_count > 0 && view)
			tw_xdg_view_activate(desktop, view);
	}
	return false;
}

static bool
resize_view(struct tw_keyboard *keyboard, uint32_t time,
            uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");
	int32_t dx = (option == RESIZE_LEFT) ? -10 :
		(option == RESIZE_RIGHT) ? 10 : 0;
	int32_t dy = (option == RESIZE_UP) ? -10 :
		(option == RESIZE_DOWN) ? 10 : 0;
	enum wl_shell_surface_resize edge =
		WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT;

	if (desktop && keyboard->focused_surface) {
		surface = tw_surface_from_resource(keyboard->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (view)
			tw_xdg_resize_view(desktop, view, dx, dy, edge);
	}
	return true;
}

static bool
switch_workspace(struct tw_keyboard *keyboard,
                 uint32_t time, uint32_t key, uint32_t modifiers,
                 uint32_t switch_left, //option
                 void *data)
{
	int idx, curr_idx;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");
	if (!desktop)
		return false;

        curr_idx = tw_xdg_current_workspace_idx(desktop);
	if (switch_left == true)
		idx = MAX(0, curr_idx-1);
	else
		idx = MIN(MAX_WORKSPACES-1, curr_idx+1);
        tw_xdg_switch_workspace(desktop, idx);

	//TODO:damage all the outputs
        return true;
}

static bool
switch_workspace_last(struct tw_keyboard *keyboard, uint32_t time,
                      uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	int prev_idx;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");
	if (!desktop)
		return false;

	prev_idx = tw_xdg_last_workspace_idx(desktop);
	tw_xdg_switch_workspace(desktop, prev_idx);
	return true;
}

static bool
toggle_view_vertical(struct tw_keyboard *keyboard, uint32_t time,
                     uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");

	if (desktop && keyboard->focused_surface) {
		surface = tw_surface_from_resource(keyboard->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (view)
			tw_xdg_toggle_view_split(desktop, view);
	}
	return true;
}

static bool
toggle_view_layout(struct tw_keyboard *keyboard, uint32_t time,
                   uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");

	if (desktop && keyboard->focused_surface) {
		surface = tw_surface_from_resource(keyboard->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (view)
			tw_xdg_toggle_view_layout(desktop, view);
	}
	return true;
}

static bool
split_desktop_view(struct tw_keyboard *keyboard, uint32_t time,
                   uint32_t key, uint32_t mods, uint32_t vsplit, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");

	if (desktop && keyboard->focused_surface) {
		surface = tw_surface_from_resource(keyboard->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (view)
			tw_xdg_split_on_view(desktop, view, vsplit);
	}
	return true;
}

static bool
merge_desktop_view(struct tw_keyboard *keyboard, uint32_t time,
                   uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_surface *surface;
	struct tw_xdg_view *view;
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");

	if (desktop && keyboard->focused_surface) {
		surface = tw_surface_from_resource(keyboard->focused_surface);
		view = tw_xdg_view_from_tw_surface(surface);
		if (view)
			tw_xdg_merge_view(desktop, view);
	}
	return true;
}

static bool
desktop_recent_view(struct tw_keyboard *keyboard, uint32_t time,
                    uint32_t key, uint32_t mods, uint32_t option, void *data)
{
	struct tw_config *config = data;
	struct tw_xdg *desktop =
		tw_config_request_object(config, "desktop");
	struct tw_seat *seat =
		container_of(keyboard, struct tw_seat, keyboard);
	if (!desktop)
		return false;
	tw_xdg_start_task_switching_grab(desktop, time, key, mods, seat);
	return true;
}

void
tw_config_default_bindings(struct tw_binding *bindings)
{
	static struct tw_binding default_bindings[TW_BUILTIN_BINDING_SIZE] = {
		[TW_QUIT_BINDING] = {
			.keypress = {{KEY_F12, 0}, {0}, {0}, {0}, {0}},
			.key_func = quit_compositor,
			.type = TW_BINDING_key,
			.name = "TW_QUIT",
		},
		[TW_CLOSE_APP_BINDING] = {
			.keypress = {{KEY_C, TW_MODIFIER_SUPER |
					TW_MODIFIER_SHIFT},
			             {0}, {0}, {0}, {0}},
			.key_func = close_application,
			.type = TW_BINDING_key,
			.name = "TW_CLOSE_APP",
		},
		[TW_RELOAD_CONFIG_BINDING] = {
			.keypress = {{KEY_R, TW_MODIFIER_CTRL |
			              TW_MODIFIER_ALT},
			             {0}, {0}, {0}, {0}},
			.key_func = reload_config,
			.type = TW_BINDING_key,
			.name = "TW_RELOAD_CONFIG",
		},
		[TW_OPEN_CONSOLE_BINDING] = {
			.keypress = {{KEY_P, TW_MODIFIER_SUPER},
			             {0}, {0}, {0}, {0}},
			.key_func = should_start_console,
			.type = TW_BINDING_key,
			.name = "TW_OPEN_CONSOLE",
		},
		[TW_ZOOM_AXIS_BINDING] = {
			.axisaction = {WL_POINTER_AXIS_VERTICAL_SCROLL,
			               TW_MODIFIER_CTRL | TW_MODIFIER_SUPER},
			.axis_func = zoom_axis,
			.type = TW_BINDING_axis,
			.name = "TW_ZOOM_AXIS",
		},
		[TW_ALPHA_AXIS_BINDING] = {
			.axisaction = {WL_POINTER_AXIS_VERTICAL_SCROLL,
			               TW_MODIFIER_CTRL | TW_MODIFIER_SHIFT},
			.axis_func = alpha_axis,
			.type = TW_BINDING_axis,
			.name = "TW_ALPHA_AXIS",
		},
		[TW_MOVE_PRESS_BINDING] = {
			.btnpress = {BTN_LEFT, TW_MODIFIER_SUPER},
			.btn_func = moving_surface_pointer,
			.type = TW_BINDING_btn,
			.name = "TW_MOVE_VIEW_BTN",
		},
		[TW_FOCUS_PRESS_BINDING] = {
			.btnpress = {BTN_LEFT, 0},
			.btn_func = click_activate_surface,
			.type = TW_BINDING_btn,
			.name = "TW_FOCUS_VIEW_BTN",
		},
		[TW_RESIZE_ON_LEFT_BINDING] = {
			.keypress = {{KEY_LEFT, TW_MODIFIER_ALT},
			             {0}, {0}, {0}, {0}},
			.key_func = resize_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_RESIZE_LEFT",
			.option = RESIZE_LEFT,
		},
		[TW_RESIZE_ON_RIGHT_BINDING] = {
			.keypress = {{KEY_RIGHT, TW_MODIFIER_ALT},
			             {0}, {0}, {0}, {0}},
			.key_func = resize_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_RESIZE_RIGHT",
			.option = RESIZE_RIGHT,
		},
		[TW_RESIZE_ON_UP_BINDING] = {
			.keypress = {{KEY_UP, TW_MODIFIER_ALT},
			             {0}, {0}, {0}, {0}},
			.key_func = resize_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_RESIZE_UP",
			.option = RESIZE_UP,
		},
		[TW_RESIZE_ON_DOWN_BINDING] = {
			.keypress = {{KEY_DOWN, TW_MODIFIER_ALT},
			             {0}, {0}, {0}, {0}},
			.key_func = resize_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_RESIZE_DOWN",
			.option = RESIZE_DOWN,
		},
		[TW_SWITCH_WS_LEFT_BINDING] = {
			.keypress = {{KEY_LEFT, TW_MODIFIER_CTRL},
			             {0}, {0}, {0}, {0}},
			.key_func = switch_workspace,
			.option = true,
			.type = TW_BINDING_key,
			.name = "TW_MOVE_TO_LEFT_WORKSPACE",
		},
		[TW_SWITCH_WS_RIGHT_BINDING] = {
			.keypress = {{KEY_RIGHT, TW_MODIFIER_CTRL},
			             {0}, {0}, {0}, {0}},
			.key_func = switch_workspace,
			.option = false,
			.type = TW_BINDING_key,
			.name = "TW_MOVE_TO_RIGHT_WORKSPACE",
		},
		[TW_SWITCH_WS_RECENT_BINDING] = {
			.keypress = {{KEY_B, TW_MODIFIER_CTRL},
			             {KEY_B, TW_MODIFIER_CTRL},
			             {0}, {0}, {0}},
			.key_func = switch_workspace_last,
			.type = TW_BINDING_key,
			.name = "TW_MOVE_TO_RECENT_WORKSPACE",
		},
		[TW_TOGGLE_FLOATING_BINDING] = {
			.keypress = {{KEY_SPACE, TW_MODIFIER_SUPER},
			             {0}, {0}, {0}, {0}},
			.key_func = toggle_view_layout,
			.type = TW_BINDING_key,
			.name = "TW_TOGGLE_FLOATING",
		},
		[TW_TOGGLE_VERTICAL_BINDING] = {
			.keypress = {{KEY_SPACE, TW_MODIFIER_ALT |
			              TW_MODIFIER_SHIFT},
			             {0}, {0}, {0}, {0}},
			.key_func = toggle_view_vertical,
			.type = TW_BINDING_key,
			.name = "TW_TOGGLE_VERTICAL",
		},
		[TW_VSPLIT_WS_BINDING] = {
			.keypress = {{KEY_V, TW_MODIFIER_SUPER},
			             {0}, {0}, {0}, {0}},
			.key_func = split_desktop_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_SPLIT_VERTICAL",
			.option = true,
		},
		[TW_HSPLIT_WS_BINDING] = {
			.keypress = {{KEY_H, TW_MODIFIER_SUPER},
			             {0}, {0}, {0}, {0}},
			.key_func = split_desktop_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_SPLIT_HORIZENTAL",
			.option = false,
		},
		[TW_MERGE_BINDING] = {
			.keypress = {{KEY_M, TW_MODIFIER_SUPER},
			             {0}, {0}, {0}, {0}},
			.key_func = merge_desktop_view,
			.type = TW_BINDING_key,
			.name = "TW_VIEW_MERGE",
		},
		[TW_NEXT_VIEW_BINDING] = {
			.keypress = {{KEY_J, TW_MODIFIER_ALT |
			              TW_MODIFIER_SHIFT},
			             {0},{0},{0},{0}},
			.key_func = desktop_recent_view,
			.type = TW_BINDING_key,
			.name = "TW_NEXT_VIEW",
		},
	};

	memcpy(bindings, default_bindings, sizeof(default_bindings));
}

bool
tw_config_install_bindings(struct tw_config_table *table)
{
	bool safe = true;
	struct tw_binding *ub;
	const struct tw_binding *b;
	struct tw_config *conf = table->config;
	struct tw_bindings *root = &table->bindings;

	for (int i = 0; i < TW_BUILTIN_BINDING_SIZE; i++) {
		b = &table->builtin_bindings[i];
		switch (b->type) {
		case TW_BINDING_key:
			safe = safe && tw_bindings_add_key(root, b->keypress,
			                                   b->key_func,
			                                   b->option, conf);
			break;
		case TW_BINDING_btn:
			safe = safe && tw_bindings_add_btn(root, &b->btnpress,
			                                   b->btn_func, conf);
			break;
		case TW_BINDING_axis:
			safe = safe && tw_bindings_add_axis(root,
			                                    &b->axisaction,
			                                    b->axis_func,
			                                    conf);
			break;
		default:
			break;
		}
		if (!safe)
			break;
	}

	vector_for_each(ub, &table->config_bindings) {
		switch (ub->type) {
		case TW_BINDING_key:
			safe = safe && tw_bindings_add_key(root, ub->keypress,
			                                   ub->key_func, 0,ub);
			break;
		case TW_BINDING_btn:
			safe = safe && tw_bindings_add_btn(root, &ub->btnpress,
			                                   ub->btn_func, ub);
			break;
		case TW_BINDING_axis:
			safe = safe && tw_bindings_add_axis(root,
			                                    &ub->axisaction,
			                                    ub->axis_func, ub);
			break;
		default:
			break;

		}
		if (!safe)
			break;
	}

	return safe;
}
