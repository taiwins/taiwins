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
#include <libweston/libweston.h>
#include "config_internal.h"
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

	if (!tw_config_run(config, NULL)) {
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
switch_workspace_recent(struct weston_keyboard *keyboard,
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

/*
static bool
desktop_add_bindings(struct tw_bindings *bindings, struct tw_config *c,
		     struct tw_apply_bindings_listener *listener)
{
	struct desktop *d = container_of(listener, struct desktop, add_binding);
	bool safe = true;
	//////////////////////////////////////////////////////////
	//move press
	struct tw_btn_press move_press =
		tw_config_get_builtin_binding(c, TW_MOVE_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &move_press, desktop_click_move, d);
	//////////////////////////////////////////////////////////
	//transparent
	struct tw_axis_motion axis_motion =
		tw_config_get_builtin_binding(c, TW_ALPHA_AXIS_BINDING)->axisaction;
	tw_bindings_add_axis(bindings, &axis_motion, desktop_alpha_axis, d);

	//////////////////////////////////////////////////////////
	//focus press
	struct tw_btn_press focus_press =
		tw_config_get_builtin_binding(c, TW_FOCUS_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &focus_press,
			    desktop_click_activate_view, d);
	tw_bindings_add_touch(bindings, 0, desktop_touch_activate_view, d);

	//////////////////////////////////////////////////////////
	//switch workspace
	const struct tw_key_press *switch_ws_left =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_LEFT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_right =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_RIGHT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_back =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_RECENT_BINDING)->keypress;
	safe = safe && tw_bindings_add_key(bindings, switch_ws_left,
					   desktop_workspace_switch,
					   true, //switch to left
					   d);
	safe = safe && tw_bindings_add_key(bindings, switch_ws_right,
					   desktop_workspace_switch,
					   false, //switch to right
					   d);
	safe = safe && tw_bindings_add_key(bindings, switch_ws_back, desktop_workspace_switch_recent,
					 0, d);

	//////////////////////////////////////////////////////////
	//resize view
	const struct tw_key_press *resize_left =
		tw_config_get_builtin_binding(c, TW_RESIZE_ON_LEFT_BINDING)->keypress;
	const struct tw_key_press *resize_right =
		tw_config_get_builtin_binding(c, TW_RESIZE_ON_RIGHT_BINDING)->keypress;
	safe = safe &&
		tw_bindings_add_key(bindings, resize_left, desktop_view_resize, RESIZE_LEFT, d);
	safe = safe &&
		tw_bindings_add_key(bindings, resize_right, desktop_view_resize, RESIZE_RIGHT, d);

	//////////////////////////////////////////////////////////
	//toggle views
	const struct tw_key_press *toggle_vertical =
		tw_config_get_builtin_binding(c, TW_TOGGLE_VERTICAL_BINDING)->keypress;
	const struct tw_key_press *toggle_floating =
		tw_config_get_builtin_binding(c, TW_TOGGLE_FLOATING_BINDING)->keypress;
	const struct tw_key_press *next_view =
		tw_config_get_builtin_binding(c, TW_NEXT_VIEW_BINDING)->keypress;
	const struct tw_key_press *vsplit =
		tw_config_get_builtin_binding(c, TW_VSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *hsplit =
		tw_config_get_builtin_binding(c, TW_HSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *merge =
		tw_config_get_builtin_binding(c, TW_MERGE_BINDING)->keypress;

	safe = safe && tw_bindings_add_key(bindings, toggle_vertical, desktop_toggle_vertical, 0, d);
	safe = safe && tw_bindings_add_key(bindings, toggle_floating, desktop_toggle_floating, 0, d);
	safe = safe && tw_bindings_add_key(bindings, next_view, desktop_recent_view, 0, d);
	safe = safe && tw_bindings_add_key(bindings, vsplit, desktop_split_view, 0, d);
	safe = safe && tw_bindings_add_key(bindings, hsplit, desktop_split_view, 1, d);
	safe = safe && tw_bindings_add_key(bindings, merge, desktop_merge_view, 0, d);

	return safe;
}
*/
