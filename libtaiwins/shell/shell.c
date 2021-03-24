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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <pixman.h>
#include <ctypes/helpers.h>
#include <shared_config.h>

#include <taiwins/objects/surface.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>

#include <taiwins/output_device.h>
#include <taiwins/shell.h>
#include <taiwins/engine.h>
#include "shell_internal.h"

#define TW_SHELL_UI_ROLE "taiwins shell_ui role"
static struct tw_shell s_shell = {0};

static void
notify_shell_ui_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_shell_ui *ui =
		container_of(listener, struct tw_shell_ui, surface_destroy);
	ui->binded = NULL;
}

struct tw_shell_ui *
shell_create_ui_element(struct tw_shell *shell,
                        struct tw_shell_ui *elem,
                        struct wl_resource *ui_resource,
                        struct tw_surface *surface,
                        struct tw_shell_output *output,
                        uint32_t x, uint32_t y,
                        struct tw_layer *layer,
                        tw_surface_commit_cb_t commit_cb)
{
	if (!elem)
		elem = calloc(1, sizeof(struct tw_shell_ui));
	if (!elem)
		return NULL;
	elem->shell = shell;
	elem->resource = ui_resource;
	elem->binded = surface;
	elem->output = output;
	elem->x = x;
	elem->y = y;
	elem->layer = layer;

	// install surface data.
	tw_reset_wl_list(&surface->layer_link);
	wl_list_insert(layer->views.prev, &surface->layer_link);
	shell_ui_set_role(elem, commit_cb, surface);
	wl_list_init(&elem->grab_close.link);
	tw_signal_setup_listener(&surface->signals.destroy,
	                         &elem->surface_destroy,
	                         notify_shell_ui_surface_destroy);

	return elem;
}

void
shell_ui_set_role(struct tw_shell_ui *ui,
                  void (*commit)(struct tw_surface *surface),
                  struct tw_surface *surface)
{
	surface->role.commit = commit;
	surface->role.commit_private = ui;
	surface->role.name = TW_SHELL_UI_ROLE;
}

static void
shell_ui_unset_role(struct tw_shell_ui *ui)
{
	if (ui->binded) {
		ui->binded->role.commit = NULL;
		ui->binded->role.commit_private = NULL;
		ui->binded->role.name = NULL;
	}
}

struct tw_shell_output *
shell_output_from_engine_output(struct tw_shell *shell,
                                struct tw_engine_output *output)
{
	struct tw_shell_output *shell_output;
	wl_list_for_each(shell_output, &shell->heads, link)
		if (shell_output->output == output)
			return shell_output;
	return NULL;
}

/******************************************************************************
 * shell interface
 *****************************************************************************/
static void
shell_change_desktop_area(void *data)
{
	struct tw_shell_output *shell_output = data;
	struct tw_engine_output *output = shell_output->output;
	struct tw_shell *shell = shell_output->shell;

	wl_signal_emit(&shell->desktop_area_signal, output);
}

static void
panel_size_changed(struct tw_shell_output *shell_output)
{
	struct tw_shell *shell = shell_output->shell;
	struct wl_event_loop *loop = wl_display_get_event_loop(shell->display);
	wl_event_loop_add_idle(loop, shell_change_desktop_area, shell_output);
}

static void
commit_panel(struct tw_surface *surface)
{
	struct tw_shell_ui *ui = surface->role.commit_private;
	struct tw_shell *shell = ui->shell;
	struct tw_shell_output *output = ui->output;
	pixman_rectangle32_t *geo = &surface->geometry.xywh;
	pixman_rectangle32_t output_geo =
		tw_output_device_geometry(output->output->device);

	//TODO: in the future, we can use ui_configure instead of
	//output_configure for the size. For now we expect surfaces to hornor
	//size.
	ui->x = output_geo.x;
	ui->y = output_geo.y;

	if (shell->panel_pos == TAIWINS_SHELL_PANEL_POS_BOTTOM)
		ui->y += (output_geo.height - geo->height);

	tw_surface_set_position(surface, ui->x, ui->y);

        if (output->panel_height != (int)geo->height)
	        panel_size_changed(output);
	output->panel_height = geo->height;
}

static void
commit_fullscreen(struct tw_surface *surface)
{
	struct tw_shell_ui *ui = surface->role.commit_private;
	struct tw_shell_output *output = ui->output;
	pixman_rectangle32_t output_geo =
		tw_output_device_geometry(output->output->device);

	ui->x = output_geo.x;
	ui->y = output_geo.y;

	tw_surface_set_position(surface, ui->x, ui->y);
}

static void
commit_widget(struct tw_surface *surface)
{
	struct tw_shell_ui *ui = surface->role.commit_private;
	tw_surface_set_position(surface, ui->x, ui->y);
}

static void
shell_ui_destroy_resource(struct wl_client *client,
                          struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct taiwins_ui_interface tw_ui_impl = {
	.destroy = shell_ui_destroy_resource,
};

static void
shell_ui_unbind(struct wl_resource *resource)
{
	//TODO: deal with all the bindings
	struct tw_shell_ui *ui  = wl_resource_get_user_data(resource);
	struct tw_shell_output *output = ui->output;

	if (ui->binded)
		tw_reset_wl_list(&ui->binded->layer_link);

	if (output && ui == &output->panel) {
		output->panel = (struct tw_shell_ui){0};
		output->panel_height = 0;
		panel_size_changed(output);
	} else if (output && ui == &output->background) {
		output->background = (struct tw_shell_ui){0};
	}
	tw_reset_wl_list(&ui->surface_destroy.link);
	tw_reset_wl_list(&ui->grab_close.link);
	ui->binded = NULL;
	ui->layer = NULL;
	ui->resource = NULL;
}

static void
shell_ui_unbind_free(struct wl_resource *resource)
{
	struct tw_shell_ui *ui = wl_resource_get_user_data(resource);
	shell_ui_unbind(resource);
	free(ui);
}

static struct tw_layer *
shell_ui_type_to_layer(struct tw_shell *shell,
                       enum taiwins_ui_type type)
{
	switch (type) {
	case TAIWINS_UI_TYPE_BACKGROUND:
		return &shell->background_layer;
	case TAIWINS_UI_TYPE_PANEL:
		return &shell->ui_layer;
	case TAIWINS_UI_TYPE_WIDGET:
		return &shell->ui_layer;
	case TAIWINS_UI_TYPE_LOCKER:
		return &shell->locker_layer;
	}
	assert(0);
	return NULL;
}

tw_surface_commit_cb_t
shell_ui_type_to_commit_cb(enum taiwins_ui_type type)
{
	switch (type) {
	case TAIWINS_UI_TYPE_PANEL:
		return commit_panel;
	case TAIWINS_UI_TYPE_BACKGROUND:
		return commit_fullscreen;
	case TAIWINS_UI_TYPE_LOCKER:
		return commit_fullscreen;
	case TAIWINS_UI_TYPE_WIDGET:
		return commit_widget;
	}
	return NULL;
}

static void
create_ui_element(struct wl_client *client,
                  struct tw_shell *shell,
                  struct tw_shell_ui *elem, uint32_t tw_ui,
                  struct wl_resource *wl_surface,
                  struct tw_shell_output *output,
                  uint32_t x, uint32_t y,
                  enum taiwins_ui_type type)
{
	bool allocated = elem == NULL;
	struct wl_resource *resource;
	struct tw_surface *surface = tw_surface_from_resource(wl_surface);
	struct tw_layer *layer = shell_ui_type_to_layer(shell, type);
	tw_surface_commit_cb_t commit_cb = shell_ui_type_to_commit_cb(type);
	resource = wl_resource_create(client, &taiwins_ui_interface, 1, tw_ui);
	//TODO, we should check if surface already have a role

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	elem = shell_create_ui_element(shell, elem, resource, surface,
	                               output, x, y, layer, commit_cb);
	if (!elem) {
		wl_resource_destroy(resource);
		wl_client_post_no_memory(client);
		return;
	}

	if (allocated)
		wl_resource_set_implementation(resource, &tw_ui_impl, elem,
		                               shell_ui_unbind_free);
	else
		wl_resource_set_implementation(resource, &tw_ui_impl, elem,
		                               shell_ui_unbind);
	elem->type = type;
	wl_signal_emit(&shell->widget_create_signal, shell);
}

/******************************************************************************
 * taiwins shell interface
 *****************************************************************************/

static void
create_shell_panel(struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t tw_ui, struct wl_resource *wl_surface,
                   int idx)
{
	//check the id
	struct tw_shell *shell = wl_resource_get_user_data(resource);
	struct tw_shell_output *output = &shell->tw_outputs[idx];
	create_ui_element(client, shell, &output->panel,
	                  tw_ui, wl_surface, output,
	                  0, 0, TAIWINS_UI_TYPE_PANEL);
}

static void
notify_shell_widget_close(struct wl_listener *listener, void *data)
{
	struct tw_shell_ui *ui =
		container_of(listener, struct tw_shell_ui, grab_close);
	taiwins_ui_send_close(ui->resource);
}

/** I should have a seat on it */
static void
launch_shell_widget(struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t tw_ui,
                    struct wl_resource *wl_surface,
                    struct wl_resource *wl_seat,
                    int32_t idx,
                    uint32_t x, uint32_t y)
{
	struct tw_shell *shell = wl_resource_get_user_data(resource);
	struct tw_shell_output *output = &shell->tw_outputs[idx];
	struct tw_seat *seat = tw_seat_from_resource(wl_seat);
	struct tw_keyboard *keyboard = &seat->keyboard;
	pixman_rectangle32_t output_geo =
		tw_output_device_geometry(output->output->device);

	x += output_geo.x;
	y += output_geo.y;
	create_ui_element(client, shell, &shell->widget, tw_ui,
	                  wl_surface, output,
	                  x, y, TAIWINS_UI_TYPE_WIDGET);
	if (shell->widget.resource) {
		tw_popup_grab_init(&shell->widget.grab,
		                   tw_surface_from_resource(wl_surface),
		                   shell->widget.resource);
		tw_signal_setup_listener(&shell->widget.grab.close,
		                         &shell->widget.grab_close,
		                         notify_shell_widget_close);
		tw_popup_grab_start(&shell->widget.grab, seat);
	}
	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		tw_keyboard_set_focus(keyboard, wl_surface, NULL);
}

static void
create_shell_background(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t tw_ui,
                        struct wl_resource *wl_surface,
                        int32_t tw_ouptut)
{
	struct tw_shell *shell = wl_resource_get_user_data(resource);
	struct tw_shell_output *shell_output = &shell->tw_outputs[tw_ouptut];

	create_ui_element(client, shell, &shell_output->background, tw_ui,
	                  wl_surface, shell_output,
			  0, 0, TAIWINS_UI_TYPE_BACKGROUND);
}

static void
create_shell_locker(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t tw_ui,
		    struct wl_resource *wl_surface,
                    UNUSED_ARG(int32_t tw_output))
{
	struct tw_shell *shell = wl_resource_get_user_data(resource);
	//TODO: I shall create locker for all the logical output
	struct tw_shell_output *shell_output =
		&shell->tw_outputs[0];
	create_ui_element(client, shell, &shell->locker, tw_ui, wl_surface,
			  shell_output, 0, 0, TAIWINS_UI_TYPE_LOCKER);
}

static void
recv_client_msg(UNUSED_ARG(struct wl_client *client),
                UNUSED_ARG(struct wl_resource *resource),
                UNUSED_ARG(uint32_t key),
                UNUSED_ARG(struct wl_array *value))
{
}

static struct taiwins_shell_interface shell_impl = {
	.create_panel = create_shell_panel,
	.create_background = create_shell_background,
	.create_locker = create_shell_locker,
	.launch_widget = launch_shell_widget,
	.server_msg = recv_client_msg,
};

/******************************************************************************
 * internal APIs
 *****************************************************************************/


static void
launch_shell_client(void *data)
{
	struct tw_shell *shell = data;

	shell->process.chld_handler = NULL;
	shell->process.user_data = shell;
	shell->shell_client = tw_launch_client(shell->display, shell->path,
	                                       &shell->process);
	wl_client_get_credentials(shell->shell_client, &shell->pid,
	                          &shell->uid,
	                          &shell->gid);
}

static inline void
shell_send_panel_pos(struct tw_shell *shell)
{
	char msg[32];
	snprintf(msg, 31, "%d",
	         shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP ?
	         TAIWINS_SHELL_PANEL_POS_TOP : TAIWINS_SHELL_PANEL_POS_BOTTOM);
	tw_shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_PANEL_POS, msg);
}

static void
shell_send_output_config(struct tw_shell *shell,
                         struct tw_engine_output *output,
                         enum taiwins_shell_output_msg msg)
{
	pixman_rectangle32_t geo = tw_output_device_geometry(output->device);
	int scale = output->device->state.scale;

	if (shell->shell_resource)
		taiwins_shell_send_output_configure(shell->shell_resource,
		                                    output->id,
		                                    geo.width, geo.height,
		                                    scale, output->id == 0,
		                                    msg);
}


static void
shell_send_default_config(struct tw_shell *shell)
{
	struct tw_engine_output *output;
	struct tw_engine *engine = shell->engine;

	wl_list_for_each(output, &engine->heads, link) {
		if (output->cloning >= 0)
			continue;
		shell_send_output_config(shell, output,
		                         TAIWINS_SHELL_OUTPUT_MSG_CONNECTED);
	}
	shell_send_panel_pos(shell);
}

/******************************************************************************
 * shell functions
 *****************************************************************************/

static void
unbind_shell(struct wl_resource *resource)
{
	//TODO using assert!
	struct tw_shell *shell = wl_resource_get_user_data(resource);
	/* struct wl_list *locker_layer = &shell->locker_layer.views; */
	/* struct wl_list *bg_layer = &shell->background_layer.views; */
	/* struct wl_list *ui_layer = &shell->ui_layer.views; */

	tw_layer_unset_position(&shell->background_layer);
	tw_layer_unset_position(&shell->ui_layer);
	tw_layer_unset_position(&shell->locker_layer);

	tw_logl("shell_unbinded!\n");
}

static void
bind_shell(struct wl_client *client, void *data,
           UNUSED_ARG(uint32_t version), uint32_t id)
{
	struct tw_shell *shell = data;
	uid_t uid; gid_t gid; pid_t pid;
	struct wl_resource *r = NULL;

	r = wl_resource_create(client, &taiwins_shell_interface,
	                       taiwins_shell_interface.version, id);

	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (shell->shell_client &&
	    (uid != shell->uid || pid != shell->pid || gid != shell->gid)) {
		wl_resource_post_error(r, WL_DISPLAY_ERROR_INVALID_OBJECT,
		                       "%d is not un atherized shell", id);
		wl_resource_destroy(r);
	}
	//only add the layers if we have a shell.

	wl_resource_set_implementation(r, &shell_impl, shell, unbind_shell);
	shell->shell_resource = r;
	shell->ready = true;

	/// send configurations to clients now
	shell_send_default_config(shell);
}

/******************************************************************************
 * exposed API
 *****************************************************************************/

WL_EXPORT void
tw_shell_create_ui_elem(struct tw_shell *shell,
                        struct wl_client *client,
                        struct tw_engine_output *output,
                        uint32_t tw_ui,
                        struct wl_resource *wl_surface,
                        uint32_t x, uint32_t y,
                        enum taiwins_ui_type type)
{
	struct tw_shell_output *shell_output;
	if (output) {
		shell_output = &shell->tw_outputs[output->id];
		create_ui_element(client, shell, NULL, tw_ui,
		                  wl_surface, shell_output, x, y, type);
	}
}

WL_EXPORT void
tw_shell_post_data(struct tw_shell *shell, uint32_t type,
		struct wl_array *msg)
{
	if (!shell->shell_resource)
		return;
	taiwins_shell_send_client_msg(shell->shell_resource,
				type, msg);
}

WL_EXPORT void
tw_shell_post_message(struct tw_shell *shell, uint32_t type, const char *msg)
{
	struct wl_array arr;
	size_t len = strlen(msg);

	arr.data = len == 0 ? NULL : (void *)msg;
	arr.size = len == 0 ? 0 : len+1;
	arr.alloc = 0;
	if (!shell->shell_resource)
		return;
	taiwins_shell_send_client_msg(shell->shell_resource, type, &arr);
}

WL_EXPORT pixman_rectangle32_t
tw_shell_output_available_space(struct tw_shell *shell,
                                struct tw_engine_output *output)
{
	pixman_rectangle32_t geo =
		tw_output_device_geometry(output->device);
	struct tw_shell_output *shell_output =
		shell_output_from_engine_output(shell, output);

	if (!shell_output || !shell_output->panel.binded)
		return geo;
	if (shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP)
		geo.y += shell_output->panel_height;
	geo.height -= shell_output->panel_height;

	return geo;
}

WL_EXPORT struct wl_signal *
tw_shell_get_desktop_area_signal(struct tw_shell *shell)
{
	return &shell->desktop_area_signal;
}

WL_EXPORT void
tw_shell_set_panel_pos(struct tw_shell *shell,
                       enum taiwins_shell_panel_pos pos)
{
	if (shell->panel_pos != pos) {
		shell->panel_pos = pos;
		shell_send_panel_pos(shell);
	}
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_shell_new_output(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output = data;
	struct tw_shell *shell = wl_container_of(listener, shell,
	                                         output_create_listener);
	struct tw_shell_output *shell_output = &shell->tw_outputs[output->id];

	shell_output->output = output;
	shell_output->shell = shell;
	shell_output->panel_height = 0;
	shell_output->id = output->id;
	wl_list_init(&shell_output->link);
	wl_list_insert(shell->heads.prev, &shell_output->link);
	shell_send_output_config(shell, output,
	                         TAIWINS_SHELL_OUTPUT_MSG_CONNECTED);
}

static void
notify_shell_remove_output(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output = data;
	struct tw_shell *shell = wl_container_of(listener, shell,
	                                         output_destroy_listener);
	struct tw_shell_output *shell_output =
		shell_output_from_engine_output(shell, output);

	assert(shell_output);
	shell_output->output = NULL;
	wl_list_remove(&shell_output->link);
	wl_list_init(&shell_output->link);
	shell_output->id = -1;
	shell_ui_unset_role(&shell_output->panel);
	shell_ui_unset_role(&shell_output->background);
	shell_send_output_config(shell, output, TAIWINS_SHELL_OUTPUT_MSG_LOST);
}

static void
notify_shell_resize_output(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output = data;
	struct tw_shell *shell =
		wl_container_of(listener, shell, output_resize_listener);
	struct tw_shell_output *shell_output =
		shell_output_from_engine_output(shell, output);
	shell_change_desktop_area(shell_output);
	shell_send_output_config(shell, output,
	                         TAIWINS_SHELL_OUTPUT_MSG_CHANGE);
}

static void
notify_shell_idle(struct wl_listener *listener, void *data)
{

	struct tw_shell *shell =
		container_of(listener, struct tw_shell, idle_listener);
	fprintf(stderr, "I should lock right now %p\n",
	        shell->locker.resource);
	if (shell->locker.resource)
		return;
	if (shell->shell_resource)
		tw_shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_LOCK, " ");
}


static void
notify_shell_end(struct wl_listener *listener, void *data)
{
	struct tw_shell *shell =
		container_of(listener, struct tw_shell,
		             display_destroy_listener);
	//listeners
	wl_list_remove(&shell->display_destroy_listener.link);
	wl_list_remove(&shell->output_create_listener.link);
	wl_list_remove(&shell->output_destroy_listener.link);
	wl_list_remove(&shell->output_resize_listener.link);
	wl_list_remove(&shell->idle_listener.link);
	//layers
	tw_layer_unset_position(&shell->background_layer);
	tw_layer_unset_position(&shell->bottom_ui_layer);
	tw_layer_unset_position(&shell->ui_layer);
	tw_layer_unset_position(&shell->locker_layer);

	wl_global_destroy(shell->shell_global);
	if (shell->layer_shell)
		wl_global_destroy(shell->layer_shell);
}


/******************************************************************************
 * constructor / destructor
 *****************************************************************************/

static void
shell_add_listeners(struct tw_shell *shell, struct tw_engine *engine)
{
	//global destructor
	tw_set_display_destroy_listener(shell->display,
	                                &shell->display_destroy_listener,
	                                notify_shell_end);

	tw_signal_setup_listener(&engine->signals.output_created,
	                         &shell->output_create_listener,
	                         notify_shell_new_output);
	tw_signal_setup_listener(&engine->signals.output_remove,
	                         &shell->output_destroy_listener,
	                         notify_shell_remove_output);
	tw_signal_setup_listener(&engine->signals.output_resized,
	                         &shell->output_resize_listener,
	                         notify_shell_resize_output);
        //idle listener
	wl_list_init(&shell->idle_listener.link);
	shell->idle_listener.notify = notify_shell_idle;
	//TODO: idle listener
	/* wl_signal_add(&ec->idle_signal, &shell->idle_listener); */

}

static void
shell_init_layers(struct tw_shell *shell, struct tw_layers_manager *layers)
{
	struct tw_layer *layer;

        tw_layer_init(&s_shell.background_layer);
	tw_layer_init(&s_shell.ui_layer);
	tw_layer_init(&s_shell.locker_layer);
	tw_layer_init(&s_shell.bottom_ui_layer);
	tw_layer_set_position(&s_shell.background_layer,
	                      TW_LAYER_POS_BACKGROUND, layers);
	tw_layer_set_position(&s_shell.bottom_ui_layer,
	                      TW_LAYER_POS_DESKTOP_BELOW_UI, layers);
	tw_layer_set_position(&s_shell.ui_layer,
	                      TW_LAYER_POS_DESKTOP_UI, layers);
	tw_layer_set_position(&s_shell.locker_layer,
	                      TW_LAYER_POS_LOCKER, layers);
	wl_list_for_each(layer, &layers->layers, link)
		tw_logl("layer position %x\n", layer->position);
}

/******************************************************************************
 * public APIS
 *****************************************************************************/

WL_EXPORT struct tw_shell *
tw_shell_create_global(struct wl_display *display,
                       struct tw_engine *engine, bool enable_layer_shell,
                       const char *path)
{
	struct wl_event_loop *loop;

	s_shell.shell_global =
		wl_global_create(display, &taiwins_shell_interface,
		                 taiwins_shell_interface.version,
		                 &s_shell,
		                 bind_shell);
	if (!s_shell.shell_global)
		return NULL;
	if (enable_layer_shell && !shell_impl_layer_shell(&s_shell, display)) {
		wl_global_destroy(s_shell.shell_global);
		s_shell.shell_global = NULL;
		return NULL;
	}

	s_shell.ready = false;
	s_shell.the_widget_surface = NULL;
	s_shell.shell_client = NULL;
	s_shell.panel_pos = TAIWINS_SHELL_PANEL_POS_TOP;
	s_shell.display = display;
	s_shell.engine = engine;
	//shell_outputs
	wl_list_init(&s_shell.heads);
	//layers
	shell_init_layers(&s_shell, &engine->layers_manager);
	//listeners
	shell_add_listeners(&s_shell, engine);
	//signals
	wl_signal_init(&s_shell.desktop_area_signal);
	wl_signal_init(&s_shell.widget_create_signal);
	wl_signal_init(&s_shell.widget_close_signal);

	if (path && (strlen(path) + 1 > NUMOF(s_shell.path)))
		return false;

	if (path) {
		strcpy(s_shell.path, path);
		loop = wl_display_get_event_loop(display);
		wl_event_loop_add_idle(loop, launch_shell_client, &s_shell);
	}

	return &s_shell;
}
