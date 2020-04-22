/*
 * shell.c - taiwins shell server functions
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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

#include <string.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <time.h>
#include <linux/input.h>
#include <helpers.h>
#include <strops.h>
#include <os/file.h>
#include <vector.h>

#include "../taiwins.h"
#include "../bindings.h"
#include "../config.h"
#include "shell.h"

/*******************************************************************************
 * shell ui
 ******************************************************************************/

struct shell_ui {
	struct shell *shell;
	struct weston_output *output;
	struct wl_resource *resource;
	struct weston_surface *binded;
	struct weston_binding *lose_keyboard;
	struct weston_binding *lose_pointer;
	struct weston_binding *lose_touch;
	uint32_t x; uint32_t y;
	struct weston_layer *layer;
	enum taiwins_ui_type type;
};

/*******************************************************************************
 * shell interface
 ******************************************************************************/
typedef OPTION(char *, path) path_option_t;
typedef OPTION(int32_t, value) int_option_t;
typedef OPTION(enum taiwins_shell_panel_pos, pos) pos_option_t;

/**
 * @brief represents tw_output
 *
 * the resource only creates for taiwins_shell object
 */
struct shell_output {
	struct weston_output *output;
	struct shell *shell;
	//ui elems
	int32_t panel_height;
	struct shell_ui background;
	struct shell_ui panel;
};

struct shell {
	uid_t uid; gid_t gid; pid_t pid;
	char path[256];
	struct tw_config *config;
	struct wl_client *shell_client;
	struct wl_resource *shell_resource;
	struct wl_global *shell_global;

	struct { /* options */
		pos_option_t pending_panel_pos;;
		int_option_t lock_countdown;
		int_option_t sleep_countdown;
		/**> invalid when empty */
		vector_t menu;
		path_option_t wallpaper_path;
		path_option_t widget_path;
	};
	struct weston_compositor *ec;
	//you probably don't want to have the layer
	struct weston_layer background_layer;
	struct weston_layer ui_layer;
	struct weston_layer locker_layer;

	struct weston_surface *the_widget_surface;
	enum taiwins_shell_panel_pos panel_pos;

	struct wl_signal output_area_signal;

	struct wl_listener compositor_destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener output_resize_listener;
	struct wl_listener idle_listener;
	struct tw_apply_bindings_listener add_binding;
	struct tw_config_component_listener config_component;
	struct tw_subprocess process;

	struct shell_ui widget;
	struct shell_ui locker;
	bool ready;
	//we deal with at most 16 outputs
	struct shell_output tw_outputs[16];

};

static struct shell s_shell;

struct shell *
tw_shell_get_global() {return &s_shell; }

static inline size_t
shell_n_outputs(struct shell *shell)
{
	for (int i = 0; i < 16; i++) {
		if (shell->tw_outputs[i].output == NULL)
			return i;
	}
	return 16;
}

static inline int
shell_ith_output(struct shell *shell, struct weston_output *output)
{
	for (int i = 0; i < 16; i++) {
		if (shell->tw_outputs[i].output == output)
			return i;
	}
	return -1;
}

static inline struct shell_output*
shell_output_from_weston_output(struct shell *shell, struct weston_output *output)
{
	for (int i = 0; i < 16; i++) {
		if (shell->tw_outputs[i].output == output)
			return &shell->tw_outputs[i];
	}
	return NULL;
}

static void
shell_output_available_space_changed(void *data)
{
	struct shell_output *output = data;
	struct shell *shell = output->shell;
	wl_signal_emit(&shell->output_area_signal, output->output);
}

/*******************************************************************************
 * default taiwins_shell data
 ******************************************************************************/

static struct tw_menu_item shell_default_menu[] = {
	{
		.endnode.title = "Application",
		.has_submenu = true,
		.len = 0,
	},
	{
		.endnode.title = "System",
		.has_submenu = true,
		.len = 1,
	},
	{
		.endnode.title = "Reconfigure",
		.has_submenu = false,
	},
};

/*******************************************************************************
 * taiwins_ui implementation
 ******************************************************************************/

static void
does_ui_lose_keyboard(struct weston_keyboard *keyboard,
                      const struct timespec *time, uint32_t key,
                      void *data)
{
	struct shell_ui *ui_elem = data;
	struct weston_surface *surface = ui_elem->binded;
	//this is a tricky part, it should be desttroyed when focus, but I am
	//not sure
	if (keyboard->focus == surface && ui_elem->lose_keyboard) {
		taiwins_ui_send_close(ui_elem->resource);
		weston_binding_destroy(ui_elem->lose_keyboard);
		ui_elem->lose_keyboard = NULL;
	}
}

static void
does_ui_lose_pointer(struct weston_pointer *pointer,
                     const struct timespec *time, uint32_t button,
                     void *data)
{
	struct shell_ui *ui_elem = data;
	struct weston_surface *surface = ui_elem->binded;
	if (pointer->focus != tw_default_view_from_surface(surface) &&
		ui_elem->lose_pointer) {
		taiwins_ui_send_close(ui_elem->resource);
		weston_binding_destroy(ui_elem->lose_pointer);
		ui_elem->lose_pointer = NULL;
	}
}

static void
does_ui_lose_touch(struct weston_touch *touch,
                   const struct timespec *time, void *data)
{
	struct shell_ui *ui_elem = data;
	struct weston_view *view =
		tw_default_view_from_surface(ui_elem->binded);
	if (touch->focus != view && ui_elem->lose_touch) {
		taiwins_ui_send_close(ui_elem->resource);
		weston_binding_destroy(ui_elem->lose_touch);
		ui_elem->lose_touch = NULL;
	}
}

static void
shell_ui_unbind(struct wl_resource *resource)
{
	struct wl_display *display;
	struct wl_event_loop *loop;
	struct shell_output *output;

	struct shell_ui *ui_elem = wl_resource_get_user_data(resource);
	struct weston_binding *bindings[] = {
		ui_elem->lose_keyboard,
		ui_elem->lose_pointer,
		ui_elem->lose_touch,
	};
	for (unsigned i = 0; i < NUMOF(bindings); i++)
		if (bindings[i] != NULL)
			weston_binding_destroy(bindings[i]);

	//clean up shell_output
	output = shell_output_from_weston_output(ui_elem->shell, ui_elem->output);
	display = ui_elem->shell->ec->wl_display;
	loop = wl_display_get_event_loop(display);
	if (output) {
		if (ui_elem == &output->panel) {
			output->panel = (struct shell_ui){0};
			output->panel_height = 0;
			wl_event_loop_add_idle(loop,
			                       shell_output_available_space_changed,
			                       output);
		} else if (ui_elem == &output->background)
			output->background = (struct shell_ui){0};
	}

	ui_elem->binded = NULL;
	ui_elem->layer = NULL;
	ui_elem->resource = NULL;
}

static void
shell_ui_unbind_free(struct wl_resource *resource)
{
	struct shell_ui *ui = wl_resource_get_user_data(resource);
	shell_ui_unbind(resource);
	free(ui);
}

static bool
shell_ui_create_with_binding(struct shell_ui *ui, struct wl_resource *taiwins_ui,
                             struct weston_surface *s)
{
	struct weston_compositor *ec = s->compositor;
	if (!ui)
		goto err_ui_create;
	struct weston_binding *k = weston_compositor_add_key_binding(
		ec, KEY_ESC, 0, does_ui_lose_keyboard, ui);
	if (!k)
		goto err_bind_keyboard;
	struct weston_binding *p = weston_compositor_add_button_binding(
		ec, BTN_LEFT, 0, does_ui_lose_pointer, ui);
	if (!p)
		goto err_bind_ptr;
	struct weston_binding *t = weston_compositor_add_touch_binding(
		ec, 0, does_ui_lose_touch, ui);
	if (!t)
		goto err_bind_touch;

	ui->lose_keyboard = k;
	ui->lose_touch = t;
	ui->lose_pointer = p;
	ui->resource = taiwins_ui;
	ui->binded = s;
	return true;
err_bind_touch:
	weston_binding_destroy(p);
err_bind_ptr:
	weston_binding_destroy(k);
err_bind_keyboard:
err_ui_create:
	return false;
}

static bool
shell_ui_create_simple(struct shell_ui *ui, struct wl_resource *taiwins_ui,
                       struct weston_surface *s)
{
	ui->resource = taiwins_ui;
	ui->binded = s;
	return true;
}

/**********************************************************************************
 * tw_output and listeners
 *********************************************************************************/

static void
shell_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct shell *shell = container_of(listener, struct shell, output_create_listener);
	size_t ith_output = shell_n_outputs(shell);
	//so far we have one output, which is good, but I think I shouldn't have
	//a global here, it doesn't make any
	if (ith_output == 16)
		return;
	/* wl_list_init(&shell->tw_outputs[ith_output].creation_link); */
	shell->tw_outputs[ith_output].output = output;
	shell->tw_outputs[ith_output].shell = shell;
	shell->tw_outputs[ith_output].panel_height = 0;
	//defer the tw_output creation if shell is not ready.
	if (shell->shell_resource)
		taiwins_shell_send_output_configure(shell->shell_resource, ith_output,
					       output->width, output->height, output->scale,
					       ith_output == 0,
					       TAIWINS_SHELL_OUTPUT_MSG_CONNECTED);
}

static void
shell_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct shell *shell = container_of(listener, struct shell, output_destroy_listener);
	int i = shell_ith_output(shell, output);
	if (i < 0)
		return;
	shell->tw_outputs[i].output = NULL;
}

static void
shell_output_resized(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct shell *shell =
		container_of(listener, struct shell, output_resize_listener);
	int index = shell_ith_output(shell, output);
	if (index < 0 || !shell->shell_resource)
		return;
	taiwins_shell_send_output_configure(shell->shell_resource, index,
				       output->width, output->height,
	                               output->scale, index == 0,
	                               TAIWINS_SHELL_OUTPUT_MSG_CHANGE);
}

static void
shell_compositor_idle(struct wl_listener *listener, void *data)
{

	struct shell *shell =
		container_of(listener, struct shell, idle_listener);
	fprintf(stderr, "oh, I should lock right now %p\n", shell->locker.resource);

	if (shell->locker.resource)
		return;
	if (shell->shell_resource)
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_LOCK, " ");
}

/********************************************************************************
 * shell_view
 *******************************************************************************/

static void
setup_view(struct weston_view *view, struct weston_layer *layer,
	   int x, int y)
{
	struct weston_surface *surface = view->surface;
	struct weston_output *output = view->output;

	struct weston_view *v, *next;
	//plan was to destroy all other views surface have, but it actually
	//distroy multiple views output has on a single output
	wl_list_for_each_safe(v, next, &surface->views, surface_link) {
		if (v->output == view->output && v != view) {
			weston_view_unmap(v);
			v->surface->committed = NULL;
			weston_surface_set_label_func(v->surface, NULL);
		}
	}
	//we shall do the testm
	weston_view_set_position(view, output->x + x, output->y + y);
	view->surface->is_mapped = true;
	view->is_mapped = true;
	//for the new created view
	if (wl_list_empty(&view->layer_link.link)) {
		weston_layer_entry_insert(&layer->view_list, &view->layer_link);
		weston_compositor_schedule_repaint(view->surface->compositor);
	}
}

static void
commit_background(struct weston_surface *surface, int sx, int sy)
{
	struct shell_ui *ui = surface->committed_private;
	//get the first view, as ui element has only one view
	struct weston_view *view =
		container_of(surface->views.next,
			     struct weston_view, surface_link);
	//it is not true for both
	if (surface->buffer_ref.buffer)
		setup_view(view, ui->layer, ui->x, ui->y);
}

static void
commit_panel(struct weston_surface *surface, int sx, int sy)
{
	struct shell_ui *ui = surface->committed_private;
	struct weston_view *view =
		container_of(surface->views.next,
			     struct weston_view, surface_link);
	struct shell_output *output =
		container_of(ui, struct shell_output, panel);
	struct shell *shell = output->shell;
	struct wl_display *display = shell->ec->wl_display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	if (!surface->buffer_ref.buffer)
		return;
	ui->y = (output->shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP) ?
		0 : output->output->height - surface->height;
	setup_view(view, ui->layer, ui->x, ui->y);

	// This is a bit unpleasent, but we would only know the size of the
	// panel at this point, for binding signals we would need to supply
	// which output, but unbinding signal we will
	if (output->panel_height != surface->height)
		wl_event_loop_add_idle(loop,
		                       shell_output_available_space_changed,
		                       output);
	output->panel_height = surface->height;
}

static void
commit_ui_surface(struct weston_surface *surface, int sx, int sy)
{
	//the sx and sy are from attach or attach_buffer attach sets pending
	//state, when commit request triggered, pending state calls
	//weston_surface_state_commit to use the sx, and sy in here the
	//confusion is that we cannot use sx and sy directly almost all the
	//time.
	struct shell_ui *ui = surface->committed_private;
	//get the first view, as ui element has only one view
	struct weston_view *view =
		container_of(surface->views.next,
		             struct weston_view, surface_link);
	//it is not true for both
	if (surface->buffer_ref.buffer)
		setup_view(view, ui->layer, ui->x, ui->y);
}

static void
commit_lock_surface(struct weston_surface *surface, int sx, int sy)
{
	struct weston_view *view;
	struct shell_ui *ui = surface->committed_private;
	wl_list_for_each(view, &surface->views, surface_link)
		setup_view(view, ui->layer, 0, 0);
}

static bool
set_surface(struct shell *shell,
	    struct weston_surface *surface, struct weston_output *output,
	    struct wl_resource *wl_resource,
	    void (*committed)(struct weston_surface *, int32_t, int32_t))
{
	//TODO, use wl_resource_get_user_data for position
	struct weston_view *view, *next;
	struct shell_ui *ui = wl_resource_get_user_data(wl_resource);
	//remember to reset the weston_surface's commit and commit_private
	if (surface->committed) {
		wl_resource_post_error(wl_resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface already have a role");
		return false;
	}
	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);

	view = weston_view_create(surface);

	surface->committed = committed;
	surface->committed_private = ui;
	surface->output = output;
	view->output = output;
	return true;
}

static bool
set_lock_surface(struct shell *shell, struct weston_surface *surface,
		 struct wl_resource *wl_resource)
{
	//TODO, use wl_resource_get_user_data for position
	struct weston_view *view, *next;
	struct shell_ui *ui = wl_resource_get_user_data(wl_resource);
	if (surface->committed) {
		wl_resource_post_error(wl_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface already have a role");
		return false;
	}
	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	for (unsigned i = 0; i < shell_n_outputs(shell); i++) {
		view = weston_view_create(surface);
		view->output = shell->tw_outputs[i].output;
	}
	surface->committed = commit_lock_surface;
	surface->committed_private = ui;
	surface->output = shell->tw_outputs[0].output;

	return true;
}

/*******************************************************************
 * taiwins_shell
 ******************************************************************/
static void
shell_ui_destroy_resource(struct wl_client *client,
			  struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static struct taiwins_ui_interface tw_ui_impl = {
	.destroy = shell_ui_destroy_resource,
};

static inline void
shell_send_panel_pos(struct shell *shell)
{
	char msg[32];
	snprintf(msg, 31, "%d", shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP ?
		 TAIWINS_SHELL_PANEL_POS_TOP : TAIWINS_SHELL_PANEL_POS_BOTTOM);
	shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_PANEL_POS, msg);
}

/*
 * pass-in empty elem for allocating resources, use the existing memory
 * otherwise
 */
static void
create_ui_element(struct wl_client *client,
		  struct shell *shell,
		  struct shell_ui *elem, uint32_t tw_ui,
		  struct wl_resource *wl_surface,
		  struct weston_output *output,
		  uint32_t x, uint32_t y,
		  enum taiwins_ui_type type)
{
	bool allocated = (elem == NULL);
	struct weston_seat *seat = tw_get_default_seat(shell->ec);
	struct weston_surface *surface = tw_surface_from_resource(wl_surface);
	weston_seat_set_keyboard_focus(seat, surface);
	struct wl_resource *tw_ui_resource = wl_resource_create(client, &taiwins_ui_interface, 1, tw_ui);
	if (!tw_ui_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	if (!elem)
		elem = zalloc(sizeof(struct shell_ui));

	if (type == TAIWINS_UI_TYPE_WIDGET)
		shell_ui_create_with_binding(elem, tw_ui_resource, surface);
	else
		shell_ui_create_simple(elem, tw_ui_resource, surface);
	if (allocated)
		wl_resource_set_implementation(tw_ui_resource, &tw_ui_impl, elem, shell_ui_unbind_free);
	else
		wl_resource_set_implementation(tw_ui_resource, &tw_ui_impl, elem, shell_ui_unbind);

	elem->shell = shell;
	elem->x = x;
	elem->y = y;
	elem->type = type;
	elem->output = output;

	switch (type) {
	case TAIWINS_UI_TYPE_PANEL:
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_panel);
		break;
	case TAIWINS_UI_TYPE_BACKGROUND:
		elem->layer = &shell->background_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_background);
		break;
	case TAIWINS_UI_TYPE_WIDGET:
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_ui_surface);
		break;
	case TAIWINS_UI_TYPE_LOCKER:
		elem->layer = &shell->locker_layer;
		set_lock_surface(shell, surface, tw_ui_resource);
		break;
	}
}

static void
create_shell_panel(struct wl_client *client,
		   struct wl_resource *resource,
		   uint32_t tw_ui,
		   struct wl_resource *wl_surface,
		   int idx)
{
	//check the id
	struct shell *shell = wl_resource_get_user_data(resource);
	struct shell_output *output = &shell->tw_outputs[idx];
	create_ui_element(client, shell, &output->panel,
			tw_ui, wl_surface, output->output,
			0, 0, TAIWINS_UI_TYPE_PANEL);
}

static void
launch_shell_widget(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t tw_ui,
		    struct wl_resource *wl_surface,
		    int32_t idx,
		    uint32_t x, uint32_t y)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	struct shell_output *output = &shell->tw_outputs[idx];
	create_ui_element(client, shell, &shell->widget, tw_ui,
			wl_surface, output->output,
			x, y, TAIWINS_UI_TYPE_WIDGET);
}

static void
create_shell_background(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t tw_ui,
			struct wl_resource *wl_surface,
			int32_t tw_ouptut)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	struct shell_output *shell_output = &shell->tw_outputs[tw_ouptut];

	create_ui_element(client, shell, &shell_output->background, tw_ui,
			wl_surface, shell_output->output,
			  0, 0, TAIWINS_UI_TYPE_BACKGROUND);
}

static void
create_shell_locker(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t tw_ui,
		    struct wl_resource *wl_surface,
		    int32_t tw_output)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	struct shell_output *shell_output =
		&shell->tw_outputs[0];
	create_ui_element(client, shell, &shell->locker, tw_ui, wl_surface,
			  shell_output->output, 0, 0, TAIWINS_UI_TYPE_LOCKER);
}

static struct taiwins_shell_interface shell_impl = {
	.create_panel = create_shell_panel,
	.create_background = create_shell_background,
	.create_locker = create_shell_locker,
	.launch_widget = launch_shell_widget,
};

static void
launch_shell_client(void *data)
{
	struct shell *shell = data;

	shell->process.chld_handler = NULL;
	shell->process.user_data = shell;
	shell->shell_client = tw_launch_client(shell->ec, shell->path,
	                                       &shell->process);
	wl_client_get_credentials(shell->shell_client, &shell->pid,
	                          &shell->uid,
	                          &shell->gid);
}

/*******************************************************************
 * bindings
 ******************************************************************/

static void
zoom_axis(struct weston_pointer *pointer, const struct timespec *time,
	   struct weston_pointer_axis_event *event, void *data)
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

static void
shell_reload_config(struct weston_keyboard *keyboard,
		    const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	struct shell *shell = data;
	if (!tw_config_run(shell->config, NULL)) {
		const char *err_msg = tw_config_retrieve_error(shell->config);
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_CONFIG_ERR, err_msg);
	}
}

static bool
shell_add_bindings(struct tw_bindings *bindings, struct tw_config *c,
		   struct tw_apply_bindings_listener *listener)
{
	//be careful, the c here is the temporary config, so as the binding
	struct shell *shell = container_of(listener, struct shell, add_binding);
	const struct tw_axis_motion motion =
		tw_config_get_builtin_binding(c, TW_ZOOM_AXIS_BINDING)->axisaction;
	const struct tw_key_press *reload_press =
		tw_config_get_builtin_binding(
			c, TW_RELOAD_CONFIG_BINDING)->keypress;
	tw_bindings_add_axis(bindings, &motion, zoom_axis, shell);
	return tw_bindings_add_key(bindings, reload_press, shell_reload_config, 0, shell);
}


static inline struct wl_array
taiwins_menu_to_wl_array(const struct tw_menu_item * items, const int len)
{
	struct wl_array serialized;
	serialized.alloc = 0;
	serialized.size = sizeof(struct tw_menu_item) * len;
	serialized.data = (void *)items;
	return serialized;
}


static inline void
shell_init_options(struct shell *shell)
{
	vector_init_zero(&shell->menu, sizeof(struct tw_menu_item), NULL);
	shell->pending_panel_pos.valid = false;
	shell->lock_countdown.valid = false;
	shell->sleep_countdown.valid = false;
	shell->wallpaper_path.path = NULL;
	shell->wallpaper_path.valid = false;
	shell->widget_path.path = NULL;
	shell->widget_path.valid = false;

}

static inline void
shell_purge_options(struct shell *shell)
{
	vector_destroy(&shell->menu);

	if (shell->wallpaper_path.path) {
		free(shell->wallpaper_path.path);
		shell->wallpaper_path.path = NULL;
	}
	shell->wallpaper_path.valid = false;

	if (shell->widget_path.path) {
		free(shell->widget_path.path);
		shell->widget_path.path = NULL;
	}
	shell->widget_path.valid = false;

	shell->pending_panel_pos.valid = false;
	shell->lock_countdown.valid = false;
	shell->sleep_countdown.valid = false;
}

static void
shell_apply_lua_config(struct tw_config *c, bool cleanup,
		       struct tw_config_component_listener *listener)
{
	struct shell *shell = container_of(listener, struct shell, config_component);
	if (cleanup)
		goto cleanup;
	if (!shell->shell_resource)
		return;

	if (shell->wallpaper_path.valid) {
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_WALLPAPER,
				   shell->wallpaper_path.path);
		shell->wallpaper_path.valid = false;
	}

	if (shell->widget_path.valid) {
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_WIDGET,
				   shell->widget_path.path);
		shell->wallpaper_path.valid = false;
	}

	if (shell->menu.len) {
		struct wl_array serialized =
			taiwins_menu_to_wl_array(shell->menu.elems, shell->menu.len);
		shell_post_data(shell, TAIWINS_SHELL_MSG_TYPE_MENU, &serialized);
		vector_destroy(&shell->menu);
	}

	if (shell->lock_countdown.valid &&
	    shell->lock_countdown.value > 1) {
		shell->ec->idle_time = shell->lock_countdown.value;
		shell->lock_countdown.valid = false;
	}

	if (shell->pending_panel_pos.valid) {
		shell->panel_pos = shell->pending_panel_pos.pos;
		shell->pending_panel_pos.valid = false;
		shell_send_panel_pos(shell);
	}

cleanup:
	shell_purge_options(shell);
}

static void
shell_send_default_config(struct shell *shell)
{
	struct weston_output *output;

	wl_list_for_each(output, &shell->ec->output_list, link) {
		int ith_output = shell_ith_output(shell, output);
		taiwins_shell_send_output_configure(shell->shell_resource,
		                                    ith_output,
		                                    output->width,
		                                    output->height,
		                                    output->scale,
		                                    ith_output == 0,
		                                    TAIWINS_SHELL_OUTPUT_MSG_CONNECTED);
	}
	if (shell->menu.len == 0) {
		struct wl_array default_menu =
			taiwins_menu_to_wl_array(shell_default_menu, 3);
		shell_post_data(shell, TAIWINS_SHELL_MSG_TYPE_MENU,
		                &default_menu);
	}
}

/*******************************************************************
 * shell function
 ******************************************************************/

static void
unbind_shell(struct wl_resource *resource)
{
	struct weston_view *v, *n;

	struct shell *shell = wl_resource_get_user_data(resource);
	weston_layer_unset_position(&shell->background_layer);
	weston_layer_unset_position(&shell->ui_layer);
	weston_layer_unset_position(&shell->locker_layer);

	wl_list_for_each_safe(v, n, &shell->locker_layer.view_list.link, layer_link.link)
		weston_view_unmap(v);
	wl_list_for_each_safe(v, n, &shell->background_layer.view_list.link, layer_link.link)
		weston_view_unmap(v);
	wl_list_for_each_safe(v, n, &shell->ui_layer.view_list.link, layer_link.link)
		weston_view_unmap(v);
	tw_logl("shell_unbinded!\n");
}

static void
bind_shell(struct wl_client *client, void *data, uint32_t version,
           uint32_t id)
{
	struct shell *shell = data;
	uid_t uid; gid_t gid; pid_t pid;
	struct wl_resource *resource = NULL;
	struct weston_layer *layer;

	resource = wl_resource_create(client, &taiwins_shell_interface,
				      taiwins_shell_interface.version, id);

	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (shell->shell_client &&
	    (uid != shell->uid || pid != shell->pid || gid != shell->gid)) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "client %d is not un atherized shell", id);
		wl_resource_destroy(resource);
	}
	wl_list_for_each(layer, &shell->ec->layer_list, link) {
		tw_logl("layer position %x\n", layer->position);
	}
	//only add the layers if we have a shell.
	weston_layer_init(&shell->background_layer, shell->ec);
	weston_layer_set_position(&shell->background_layer, WESTON_LAYER_POSITION_BACKGROUND);
	weston_layer_init(&shell->ui_layer, shell->ec);
	weston_layer_set_position(&shell->ui_layer, WESTON_LAYER_POSITION_UI);
	weston_layer_init(&shell->locker_layer, shell->ec);
	weston_layer_set_position(&shell->locker_layer, WESTON_LAYER_POSITION_LOCK);

	wl_resource_set_implementation(resource, &shell_impl, shell, unbind_shell);
	shell->shell_resource = resource;
	shell->ready = true;

	/// send configurations to clients now
	shell_send_default_config(shell);
	shell_apply_lua_config(shell->config, false, &shell->config_component);
}

/*******************************************************************************
 * exposed API
 ******************************************************************************/

void
shell_create_ui_elem(struct shell *shell,
		     struct wl_client *client,
		     uint32_t tw_ui,
		     struct wl_resource *wl_surface,
		     uint32_t x, uint32_t y,
		     enum taiwins_ui_type type)
{
	struct weston_output *output = tw_get_focused_output(shell->ec);
	create_ui_element(client, shell, NULL, tw_ui, wl_surface, output,
			  x, y, type);
}

void
shell_post_data(struct shell *shell, uint32_t type,
		struct wl_array *msg)
{
	taiwins_shell_send_shell_msg(shell->shell_resource,
				type, msg);
}

void
shell_post_message(struct shell *shell, uint32_t type, const char *msg)
{
	struct wl_array arr;
	size_t len = strlen(msg);
	arr.data = len == 0 ? NULL : (void *)msg;
	arr.size = len == 0 ? 0 : len+1;
	arr.alloc = 0;
	taiwins_shell_send_shell_msg(shell->shell_resource, type, &arr);
}

struct weston_geometry
shell_output_available_space(struct shell *shell, struct weston_output *output)
{
	struct weston_geometry geo = {
		output->x, output->y,
		output->width, output->height
	};
	struct shell_output *shell_output =
		shell_output_from_weston_output(shell, output);

	if (!shell_output || !shell_output->panel.binded)
		return geo;
	if (shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP)
		geo.y += shell_output->panel_height;
	geo.height -= shell_output->panel_height;
	return geo;
}

void
shell_add_desktop_area_listener(struct shell *shell, struct wl_listener *listener)
{
	wl_signal_add(&shell->output_area_signal, listener);
}

/*******************************************************************************
 * constructor / destructor
 ******************************************************************************/

static void
end_shell(struct wl_listener *listener, void *data)
{
	struct shell *shell =
		container_of(listener, struct shell,
			     compositor_destroy_listener);
	//clean up resources
	shell_apply_lua_config(NULL, true, &shell->config_component);

	wl_global_destroy(shell->shell_global);
}

static void
shell_add_listeners(struct shell *shell)
{
	struct weston_compositor *ec = shell->ec;
	//global destructor
	wl_list_init(&shell->compositor_destroy_listener.link);
	shell->compositor_destroy_listener.notify = end_shell;
	wl_signal_add(&ec->destroy_signal, &shell->compositor_destroy_listener);
	//idle listener
	wl_list_init(&shell->idle_listener.link);
	shell->idle_listener.notify = shell_compositor_idle;
	wl_signal_add(&ec->idle_signal, &shell->idle_listener);

	//output create
	wl_list_init(&shell->output_create_listener.link);
	shell->output_create_listener.notify = shell_output_created;
	//output destroy
	wl_list_init(&shell->output_destroy_listener.link);
	shell->output_destroy_listener.notify = shell_output_destroyed;
	//output resize
	wl_list_init(&shell->output_resize_listener.link);
	shell->output_resize_listener.notify = shell_output_resized;
	//singals
	wl_signal_add(&ec->output_created_signal, &shell->output_create_listener);
	wl_signal_add(&ec->output_destroyed_signal, &shell->output_destroy_listener);
	wl_signal_add(&ec->output_resized_signal, &shell->output_resize_listener);
	//init current outputs
	struct weston_output *output;
	wl_list_for_each(output, &ec->output_list, link)
		shell_output_created(&shell->output_create_listener, output);
}

/*******************************************************************************
 * public APIS
 ******************************************************************************/
void
tw_shell_set_wallpaper(struct shell *shell, const char *wp)
{
	if (shell->wallpaper_path.path &&
	    !strcmp(shell->wallpaper_path.path, wp))
		return;
	if (shell->wallpaper_path.path)
		free(shell->wallpaper_path.path);
	shell->wallpaper_path.path = strdup(wp);
	shell->wallpaper_path.valid = true;
}

void
tw_shell_set_widget_path(struct shell *shell, const char *path)
{
	if (shell->widget_path.path &&
	    !strcmp(shell->widget_path.path, path))
		return;

	if (shell->widget_path.path)
		free(shell->widget_path.path);
	shell->widget_path.path = strdup(path);
	shell->widget_path.valid = true;
}

void
tw_shell_set_panel_pos(struct shell *shell, enum taiwins_shell_panel_pos pos)
{
	if (shell->panel_pos != pos) {
		shell->pending_panel_pos.pos = pos;
		shell->pending_panel_pos.valid = true;
	}
}

void
tw_shell_set_menu(struct shell *shell, vector_t *menu)
{
	vector_destroy(&shell->menu);
	shell->menu = *menu;
}

bool
tw_setup_shell(struct weston_compositor *ec, const char *path,
               struct tw_config *config)
{
	s_shell.ec = ec;
	s_shell.ready = false;
	s_shell.the_widget_surface = NULL;
	s_shell.shell_client = NULL;
	s_shell.config = config;
	s_shell.panel_pos = TAIWINS_SHELL_PANEL_POS_TOP;

	wl_signal_init(&s_shell.output_area_signal);

	if (path && (strlen(path) + 1 > NUMOF(s_shell.path)))
		return false;

	s_shell.shell_global =
		wl_global_create(ec->wl_display,
		                 &taiwins_shell_interface,
		                 taiwins_shell_interface.version,
		                 &s_shell,
		                 bind_shell);
	if (path) {
		strcpy(s_shell.path, path);
		struct wl_event_loop *loop = wl_display_get_event_loop(ec->wl_display);
		wl_event_loop_add_idle(loop, launch_shell_client, &s_shell);
	}
	shell_add_listeners(&s_shell);
	shell_init_options(&s_shell);

	//binding
	wl_list_init(&s_shell.add_binding.link);
	s_shell.add_binding.apply = shell_add_bindings;
	tw_config_add_apply_bindings(config, &s_shell.add_binding);
	//config_componenet
	wl_list_init(&s_shell.config_component.link);
	s_shell.config_component.apply = shell_apply_lua_config;
	tw_config_add_component(config, &s_shell.config_component);

	return true;
}
