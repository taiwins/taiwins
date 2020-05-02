/*
 * theme_lua.c - taiwins config bindings implementation
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

#include <helpers.h>
#include <linux/input.h>
#include <libweston/libweston.h>
#include "config_internal.h"
#include "server/bindings.h"
#include "server/config.h"
#include "server/desktop/desktop.h"
#include "server/taiwins.h"

/* TW_ZOOM_AXIS_BINDING */
void
zoom_axis(struct weston_pointer *pointer,
          UNUSED_ARG(const struct timespec *time),
          struct weston_pointer_axis_event *event, UNUSED_ARG(void *data))
{
	struct weston_compositor *ec = pointer->seat->compositor;
	double augment;
	struct weston_output *output;
	struct weston_seat *seat = pointer->seat;

	wl_list_for_each(output, &ec->output_list, link) {
		if (pixman_region32_contains_point(&output->region,
						   wl_fixed_to_int(pointer->x),
						   wl_fixed_to_int(pointer->y), NULL))
		{
			float sign = (event->has_discrete) ? -1.0 : 1.0;

			if (event->axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
				augment = output->zoom.increment * sign * event->value / 20.0;
			else
				augment = 0.0;

			output->zoom.level += augment;

			if (output->zoom.level < 0.0)
				output->zoom.level = 0.0;
			else if (output->zoom.level > output->zoom.max_level)
				output->zoom.level = output->zoom.max_level;

			if (!output->zoom.active) {
				if (output->zoom.level <= 0.0)
					continue;
				weston_output_activate_zoom(output, seat);
			}

			output->zoom.spring_z.target = output->zoom.level;
			weston_output_update_zoom(output);
		}
	}
}

/* TW_RELOAD_CONFIG_BINDING */
void
reload_config(UNUSED_ARG( struct weston_keyboard *keyboard ),
              UNUSED_ARG( const struct timespec *time ),
              UNUSED_ARG( uint32_t key ), UNUSED_ARG( uint32_t option ),
              void *data)
{
	struct tw_config *config = data;
	struct shell *shell = tw_config_request_object(config, "shell");

	if (!tw_run_config(config)) {
		const char *err_msg = tw_config_retrieve_error(config);
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_CONFIG_ERR,
		                   err_msg);
	}
}

/* TW_OPEN_CONSOLE_BINDING */
void
should_start_console(UNUSED_ARG(struct weston_keyboard *keyboard),
                     UNUSED_ARG(const struct timespec *time),
                     UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                     void *data)
{
	struct tw_config *config = data;
	struct console *console =
		tw_config_request_object(config, "console");

	tw_console_start_client(console);
}

void
alpha_axis(struct weston_pointer *pointer,
           UNUSED_ARG(const struct timespec *time),
           UNUSED_ARG(struct weston_pointer_axis_event *event),
           void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");

	if (!pointer->focus)
		return;
	tw_desktop_start_transparency_grab(desktop, pointer);
}

void
moving_surface_pointer(struct weston_pointer *pointer,
                       UNUSED_ARG(const struct timespec *time),
                       UNUSED_ARG(uint32_t button),
                       void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	struct weston_view *view = pointer->focus;
	if (pointer->button_count > 0 && view &&
	    pointer->grab == &pointer->default_grab)
		tw_desktop_start_moving_grab(desktop, pointer);
}

void
click_activate_surface(struct weston_pointer *pointer,
                       UNUSED_ARG(const struct timespec *time),
                       UNUSED_ARG(uint32_t button), void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	struct weston_view *view = pointer->focus;
	if (!view)
		return;
	if (tw_desktop_activate_view(desktop, view))
		weston_view_activate(view, pointer->seat,
		                     WESTON_ACTIVATE_FLAG_CLICKED);
}

void
touch_activate_view(struct weston_touch *touch,
                    UNUSED_ARG(const struct timespec *time),
                    void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");

	if (touch->grab != &touch->default_grab || !touch->focus)
		return;

	if (tw_desktop_activate_view(desktop, touch->focus))
		weston_view_activate(touch->focus, touch->seat,
		                     WESTON_ACTIVATE_FLAG_CLICKED);
}

void
switch_workspace(struct weston_keyboard *keyboard,
                 UNUSED_ARG(const struct timespec *time),
                 UNUSED_ARG(uint32_t key), uint32_t switch_left, //option
                 void *data)
{
	struct weston_view *view;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	int ws_idx;
	int curr_ws = tw_desktop_get_current_workspace(desktop);
	int n_ws = tw_desktop_num_workspaces(desktop);

	if (switch_left == true)
		ws_idx = MAX(0, curr_ws-1);
	else
		ws_idx = MIN(n_ws, curr_ws+1);
	view = tw_desktop_switch_workspace(desktop, ws_idx);

	if (keyboard->focus)
		if (keyboard->focus)
			tw_lose_surface_focus(keyboard->focus);
	if (view)
		weston_keyboard_set_focus(keyboard, view->surface);

	weston_compositor_damage_all(keyboard->seat->compositor);
	weston_compositor_schedule_repaint(keyboard->seat->compositor);
}


void
switch_workspace_last(struct weston_keyboard *keyboard,
                      UNUSED_ARG(const struct timespec *time),
                      UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                      void *data)
{
	struct weston_view *view;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	int last_ws = tw_desktop_get_last_workspace(desktop);

        view = tw_desktop_switch_workspace(desktop, last_ws);
	if (keyboard->focus)
		if (keyboard->focus)
			tw_lose_surface_focus(keyboard->focus);
	if (view)
		weston_keyboard_set_focus(keyboard, view->surface);

	weston_compositor_damage_all(keyboard->seat->compositor);
	weston_compositor_schedule_repaint(keyboard->seat->compositor);
}

void
resize_view(struct weston_keyboard *keyboard,
            UNUSED_ARG(const struct timespec *time),
            UNUSED_ARG(uint32_t key), uint32_t option,
            void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	struct weston_surface *surface = keyboard->focus;
	struct weston_view *focused = (surface) ?
		tw_default_view_from_surface(surface) : NULL;

        if (!focused)
		return;

        tw_desktop_start_resize_grab(desktop, focused, option);

}

void
toggle_view_split(struct weston_keyboard *keyboard,
                  UNUSED_ARG(const struct timespec *time),
                  UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                  void *data)
{
	struct weston_view *view;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");

	if (!keyboard->focus)
		return;
	view = tw_default_view_from_surface(keyboard->focus);
        tw_desktop_toggle_view_split(desktop, view);
}

void
toggle_view_layout(struct weston_keyboard *keyboard,
                  UNUSED_ARG(const struct timespec *time),
                  UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                  void *data)
{
	struct weston_view *view;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");

	if (!keyboard->focus)
		return;
	view = tw_default_view_from_surface(keyboard->focus);
	tw_desktop_toggle_view_layout(desktop, view);
}

void
split_desktop_view(struct weston_keyboard *keyboard,
                   UNUSED_ARG(const struct timespec *time),
                   UNUSED_ARG(uint32_t key),
                   uint32_t option, void *data)
{
	struct weston_surface *surface = keyboard->focus;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	if (!surface)
		return;

	tw_desktop_split_on_view(desktop, tw_default_view_from_surface(surface),
	                         option == 0);
}

void
merge_desktop_view(struct weston_keyboard *keyboard,
                   UNUSED_ARG(const struct timespec *time),
                   UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                   void *data)
{
	struct weston_surface *surface = keyboard->focus;
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	if (!surface)
		return;
	tw_desktop_merge_view(desktop, tw_default_view_from_surface(surface));
}

void
desktop_recent_view(struct weston_keyboard *keyboard,
                    UNUSED_ARG(const struct timespec *time),
                    UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                    void *data)
{
	struct tw_config *config = data;
	struct desktop *desktop =
		tw_config_request_object(config, "desktop");
	if (keyboard->grab != &keyboard->default_grab)
		return;
	tw_desktop_start_task_switch_grab(desktop, keyboard);
}

static void
quit_compositor(UNUSED_ARG(struct weston_keyboard *keyboard),
                UNUSED_ARG(const struct timespec *time),
                UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                void *data)
{
	struct tw_config *config = data;
	struct weston_compositor *compositor = config->compositor;
	struct wl_display *wl_display = compositor->wl_display;

	tw_logl("%s: quit taiwins", "taiwins");
	wl_display_terminate(wl_display);
}

void
tw_config_default_bindings(struct tw_config *c)
{
	//apply bindings
	c->builtin_bindings[TW_QUIT_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_F12, 0}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_QUIT",
	};
	c->builtin_bindings[TW_RELOAD_CONFIG_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_R, MODIFIER_CTRL | MODIFIER_ALT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_RELOAD_CONFIG",
	};
	c->builtin_bindings[TW_OPEN_CONSOLE_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_P, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_OPEN_CONSOLE",
	};
	c->builtin_bindings[TW_ZOOM_AXIS_BINDING] = (struct tw_binding) {
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SUPER},
		.type = TW_BINDING_axis,
		.name = "TW_ZOOM_AXIS",
	};
	c->builtin_bindings[TW_ALPHA_AXIS_BINDING] = (struct tw_binding) {
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SHIFT},
		.type = TW_BINDING_axis,
		.name = "TW_ALPHA_AXIS",
	};
	c->builtin_bindings[TW_MOVE_PRESS_BINDING] = (struct tw_binding) {
		.btnpress = {BTN_LEFT, MODIFIER_SUPER},
		.type = TW_BINDING_btn,
		.name = "TW_MOVE_VIEW_BTN",
	};
	c->builtin_bindings[TW_FOCUS_PRESS_BINDING] = (struct tw_binding) {
		.btnpress = {BTN_LEFT, 0},
		.type = TW_BINDING_btn,
		.name = "TW_FOCUS_VIEW_BTN",
	};
	c->builtin_bindings[TW_SWITCH_WS_LEFT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_LEFT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_LEFT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RIGHT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RIGHT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RECENT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_B, MODIFIER_CTRL}, {KEY_B, MODIFIER_CTRL},
			     {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RECENT_WORKSPACE",
	};
	c->builtin_bindings[TW_TOGGLE_FLOATING_BINDING] = (struct tw_binding){
		.keypress = {{KEY_SPACE, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_FLOATING",
	};
	c->builtin_bindings[TW_TOGGLE_VERTICAL_BINDING] = (struct tw_binding){
		.keypress = {{KEY_SPACE, MODIFIER_ALT | MODIFIER_SHIFT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_VERTICAL",
	};
	c->builtin_bindings[TW_VSPLIT_WS_BINDING] = (struct tw_binding){
		.keypress = {{KEY_V, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_VERTICAL",
	};
	c->builtin_bindings[TW_HSPLIT_WS_BINDING] = (struct tw_binding){
		.keypress = {{KEY_H, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_HORIZENTAL",
	};
	c->builtin_bindings[TW_MERGE_BINDING] = (struct tw_binding){
		.keypress = {{KEY_M, MODIFIER_SUPER},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_MERGE",
	};
	c->builtin_bindings[TW_RESIZE_ON_LEFT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_LEFT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_LEFT",
	};
	c->builtin_bindings[TW_RESIZE_ON_RIGHT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_RIGHT",
	};
	c->builtin_bindings[TW_NEXT_VIEW_BINDING] = (struct tw_binding){
		.keypress = {{KEY_J, MODIFIER_ALT | MODIFIER_SHIFT},{0},{0},{0},{0}},
		.type = TW_BINDING_key,
		.name = "TW_NEXT_VIEW",
	};
}

bool
tw_config_install_bindings(struct tw_config *c, struct tw_bindings *root)
{
	bool safe = true;
	struct tw_binding *ub;
	const struct tw_binding *b;
	const struct tw_key_press *keypress;
	const struct tw_btn_press *btnpress;
	const struct tw_axis_motion *axisaction;

	b = tw_config_get_builtin_binding(c, TW_QUIT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, quit_compositor, 0, c))
		return false;

	b = tw_config_get_builtin_binding(c, TW_RELOAD_CONFIG_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, reload_config, 0, c))
		return false;

	b = tw_config_get_builtin_binding(c, TW_OPEN_CONSOLE_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, should_start_console, 0, c))
		return false;

	b = tw_config_get_builtin_binding(c, TW_ZOOM_AXIS_BINDING);
	axisaction = &b->axisaction;
	if (!tw_bindings_add_axis(root, axisaction, zoom_axis, c))
		return false;

	b = tw_config_get_builtin_binding(c, TW_ALPHA_AXIS_BINDING);
	axisaction = &b->axisaction;
	if (!tw_bindings_add_axis(root, axisaction, alpha_axis, c))
		return false;

        b = tw_config_get_builtin_binding(c, TW_MOVE_PRESS_BINDING);
	btnpress = &b->btnpress;
	if (!tw_bindings_add_btn(root, btnpress, moving_surface_pointer, c))
		return false;

        b = tw_config_get_builtin_binding(c, TW_FOCUS_PRESS_BINDING);
	btnpress = &b->btnpress;
	if (!tw_bindings_add_btn(root, btnpress, click_activate_surface, c))
		return false;

        b = tw_config_get_builtin_binding(c, TW_SWITCH_WS_LEFT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, switch_workspace, true, c))
		return false;

	b = tw_config_get_builtin_binding(c, TW_SWITCH_WS_RIGHT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, switch_workspace, false, c))
		return false;

	b = tw_config_get_builtin_binding(c,TW_SWITCH_WS_RECENT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, switch_workspace_last, 0, c))
		return false;

        b = tw_config_get_builtin_binding(c,TW_TOGGLE_FLOATING_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, toggle_view_layout, 0, c))
		return false;

	b = tw_config_get_builtin_binding(c,TW_TOGGLE_VERTICAL_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, toggle_view_split, 0, c))
		return false;

	b = tw_config_get_builtin_binding(c,TW_VSPLIT_WS_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, split_desktop_view, 0, c))
		return false;

        b = tw_config_get_builtin_binding(c,TW_HSPLIT_WS_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, split_desktop_view, 1, c))
		return false;

        b = tw_config_get_builtin_binding(c,TW_MERGE_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, merge_desktop_view, 0, c))
		return false;

        b = tw_config_get_builtin_binding(c,TW_RESIZE_ON_LEFT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, resize_view, RESIZE_LEFT, c))
		return false;

        b = tw_config_get_builtin_binding(c,TW_RESIZE_ON_RIGHT_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, resize_view, RESIZE_RIGHT, c))
		return false;

	b = tw_config_get_builtin_binding(c,TW_NEXT_VIEW_BINDING);
	keypress = b->keypress;
	if (!tw_bindings_add_key(root, keypress, desktop_recent_view, 0, c))
		return false;

        vector_for_each(ub, &c->config_bindings) {
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
