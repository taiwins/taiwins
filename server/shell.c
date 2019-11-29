#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <helpers.h>
#include <time.h>
#include <linux/input.h>
#include <strops.h>
#include <os/file.h>

#include "taiwins.h"
#include "desktop.h"
#include "bindings.h"
#include "config.h"

/*******************************************************************************************
 * shell ui
 *******************************************************************************************/

struct shell_ui {
	struct shell *shell;
	struct wl_resource *resource;
	struct weston_surface *binded;
	struct weston_binding *lose_keyboard;
	struct weston_binding *lose_pointer;
	struct weston_binding *lose_touch;
	uint32_t x; uint32_t y;
	struct weston_layer *layer;
	enum tw_ui_type type;
};

/*******************************************************************************************
 * shell interface
 *******************************************************************************************/


/**
 * @brief represents tw_output
 *
 * the resource only creates for tw_shell object
 */
struct shell_output {
	struct weston_output *output;
	/* struct wl_list creation_link; //used when new output is created and client is not ready */
	struct shell *shell;
	//ui elems
	struct shell_ui background;
	struct shell_ui panel;
};

struct shell {
	uid_t uid; gid_t gid; pid_t pid;
	char path[256];
	struct taiwins_config *config;
	struct wl_client *shell_client;
	struct wl_resource *shell_resource;
	struct wl_global *shell_global;

	struct { /* options */
		enum tw_shell_panel_pos panel_pos;
		int32_t lock_countdown; //invalid -1
		int32_t sleep_countdown; //knvalid -1
		vector_t menu;
		const char *wallpaper_path;
		const char *widget_path;
	};
	struct weston_compositor *ec;
	//you probably don't want to have the layer
	struct weston_layer background_layer;
	struct weston_layer ui_layer;
	struct weston_layer locker_layer;

	//the widget is the global view
	struct weston_surface *the_widget_surface;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener output_resize_listener;
	struct wl_listener idle_listener;
	struct taiwins_apply_bindings_listener add_binding;
	struct taiwins_config_component_listener config_component;

	struct shell_ui widget;
	struct shell_ui locker;
	bool ready;
	//we deal with at most 16 outputs
	struct shell_output tw_outputs[16];

};

//we could make it static as well.
static struct shell oneshell;

/*******************************************************************
 * tw_ui implementation
 ******************************************************************/

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
		tw_ui_send_close(ui_elem->resource);
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
		tw_ui_send_close(ui_elem->resource);
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
		tw_ui_send_close(ui_elem->resource);
		weston_binding_destroy(ui_elem->lose_touch);
		ui_elem->lose_touch = NULL;
	}
}

static void
shell_ui_unbind(struct wl_resource *resource)
{
	struct shell_ui *ui_elem = wl_resource_get_user_data(resource);
	struct weston_binding *bindings[] = {
		ui_elem->lose_keyboard,
		ui_elem->lose_pointer,
		ui_elem->lose_touch,
	};
	for (int i = 0; i < NUMOF(bindings); i++)
		if (bindings[i] != NULL)
			weston_binding_destroy(bindings[i]);
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
shell_ui_create_with_binding(struct shell_ui *ui, struct wl_resource *tw_ui, struct weston_surface *s)
{
	struct weston_compositor *ec = s->compositor;
	if (!ui)
		goto err_ui_create;
	struct weston_binding *k = weston_compositor_add_key_binding(ec, KEY_ESC, 0, does_ui_lose_keyboard, ui);
	if (!k)
		goto err_bind_keyboard;
	struct weston_binding *p = weston_compositor_add_button_binding(ec, BTN_LEFT, 0, does_ui_lose_pointer, ui);
	if (!p)
		goto err_bind_ptr;
	struct weston_binding *t = weston_compositor_add_touch_binding(ec, 0, does_ui_lose_touch, ui);
	if (!t)
		goto err_bind_touch;

	ui->lose_keyboard = k;
	ui->lose_touch = t;
	ui->lose_pointer = p;
	ui->resource = tw_ui;
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
shell_ui_create_simple(struct shell_ui *ui, struct wl_resource *tw_ui, struct weston_surface *s)
{
	ui->resource = tw_ui;
	ui->binded = s;
	return true;
}


/*******************************************************************************************
 * tw_output and listeners
 *******************************************************************************************/

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

	//defer the tw_output creation if shell is not ready.
	if (shell->shell_resource)
		tw_shell_send_output_configure(shell->shell_resource, ith_output,
					       output->width, output->height, output->scale,
					       ith_output == 0,
					       TW_SHELL_OUTPUT_MSG_CONNECTED);
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
	struct shell *shell = container_of(listener, struct shell, output_resize_listener);
	uint32_t index = shell_ith_output(shell, output);
	if (index < 0 || !shell->shell_resource)
		return;
	tw_shell_send_output_configure(shell->shell_resource, index,
				       output->width, output->height, output->scale,
				       index == 0, TW_SHELL_OUTPUT_MSG_CHANGE);
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
		shell_post_message(shell, TW_SHELL_MSG_TYPE_LOCK, " ");
}

/*******************************************************************************************
 * shell_view
 *******************************************************************************************/


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
	//the
	if (!surface->buffer_ref.buffer)
		return;
	ui->y = (output->shell->panel_pos == TW_SHELL_PANEL_POS_TOP) ?
		0 : output->output->height - surface->height;
	setup_view(view, ui->layer, ui->x, ui->y);
}

static void
commit_ui_surface(struct weston_surface *surface, int sx, int sy)
{
	//the sx and sy are from attach or attach_buffer attach sets pending
	//state, when commit request triggered, pending state calls
	//weston_surface_state_commit to use the sx, and sy in here
	//the confusion is that we cannot use sx and sy directly almost all the time.
	struct shell_ui *ui = surface->committed_private;
	//get the first view, as ui element has only one view
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
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
	for (int i = 0; i < shell_n_outputs(shell); i++) {
		view = weston_view_create(surface);
		view->output = shell->tw_outputs[i].output;
	}
	surface->committed = commit_lock_surface;
	surface->committed_private = ui;
	surface->output = shell->tw_outputs[0].output;

	return true;
}

/*******************************************************************
 * tw_shell
 ******************************************************************/
static void
shell_ui_destroy_resource(struct wl_client *client,
			  struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}


static struct tw_ui_interface tw_ui_impl = {
	.destroy = shell_ui_destroy_resource,
};



static inline void
shell_send_panel_pos(struct shell *shell)
{
	char msg[32];
	snprintf(msg, 31, "%d", shell->panel_pos == TW_SHELL_PANEL_POS_TOP ?
		 TW_SHELL_PANEL_POS_TOP : TW_SHELL_PANEL_POS_BOTTOM);
	shell_post_message(shell, TW_SHELL_MSG_TYPE_PANEL_POS, msg);
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
		  enum tw_ui_type type)
{
	bool allocated = (elem == NULL);
	struct weston_seat *seat = tw_get_default_seat(shell->ec);
	struct weston_surface *surface = tw_surface_from_resource(wl_surface);
	weston_seat_set_keyboard_focus(seat, surface);
	struct wl_resource *tw_ui_resource = wl_resource_create(client, &tw_ui_interface, 1, tw_ui);
	if (!tw_ui_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	if (!elem)
		elem = zalloc(sizeof(struct shell_ui));

	if (type == TW_UI_TYPE_WIDGET)
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

	switch (type) {
	case TW_UI_TYPE_PANEL:
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_panel);
		break;
	case TW_UI_TYPE_BACKGROUND:
		elem->layer = &shell->background_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_background);
		break;
	case TW_UI_TYPE_WIDGET:
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_ui_surface);
		break;
	case TW_UI_TYPE_LOCKER:
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
			0, 0, TW_UI_TYPE_PANEL);
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
			x, y, TW_UI_TYPE_WIDGET);
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
			  0, 0, TW_UI_TYPE_BACKGROUND);
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
			  shell_output->output, 0, 0, TW_UI_TYPE_LOCKER);
}

static struct tw_shell_interface shell_impl = {
	.create_panel = create_shell_panel,
	.create_background = create_shell_background,
	.create_locker = create_shell_locker,
	.launch_widget = launch_shell_widget,
};

static void
launch_shell_client(void *data)
{
	struct shell *shell = data;
	shell->shell_client = tw_launch_client(shell->ec, shell->path);
	wl_client_get_credentials(shell->shell_client, &shell->pid, &shell->uid, &shell->gid);
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
	if (!taiwins_run_config(shell->config, NULL)) {
		const char *err_msg = taiwins_config_retrieve_error(shell->config);
		shell_post_message(shell, TW_SHELL_MSG_TYPE_CONFIG_ERR, err_msg);
	}
}

static bool
shell_add_bindings(struct tw_bindings *bindings, struct taiwins_config *c,
		   struct taiwins_apply_bindings_listener *listener)
{
	//be careful, the c here is the temporary config, so as the binding
	struct shell *shell = container_of(listener, struct shell, add_binding);
	const struct tw_axis_motion motion =
		taiwins_config_get_builtin_binding(c, TW_ZOOM_AXIS_BINDING)->axisaction;
	const struct tw_key_press *reload_press =
		taiwins_config_get_builtin_binding(
			c, TW_RELOAD_CONFIG_BINDING)->keypress;
	tw_bindings_add_axis(bindings, &motion, zoom_axis, shell);
	return tw_bindings_add_key(bindings, reload_press, shell_reload_config, 0, shell);
}

/*******************************************************************
 * lua components
 ******************************************************************/
#define METATABLE_SHELL "metatable_shell"
#define REGISTRY_SHELL "__shell"

static struct shell *
_lua_to_shell(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);
	struct shell *sh = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sh;
}

static int
_lua_set_wallpaper(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	_lua_stackcheck(L, 2);
	const char *path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "wallpaper does not exist!");
	if (shell->wallpaper_path)
		free((void *)shell->wallpaper_path);
	shell->wallpaper_path = strdup(path);
	return 0;
}

static int
_lua_set_widgets(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	_lua_stackcheck(L, 2);
	const char *path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "widget path does not exist!");
	if (shell->widget_path)
		free((void *)shell->widget_path);
	shell->widget_path = strdup(path);
	return 0;
}

static int
_lua_set_panel_position(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	luaL_checktype(L, 2, LUA_TSTRING);
	const char *pos = lua_tostring(L, 2);
	if (strcmp(pos, "bottom") == 0)
		shell->panel_pos = TW_SHELL_PANEL_POS_BOTTOM;
	else if (strcmp(pos, "top") == 0)
		shell->panel_pos = TW_SHELL_PANEL_POS_TOP;
	else
		luaL_error(L, "invalid panel position %s", pos);
	return 0;
}

static inline struct wl_array
taiwins_menu_to_wl_array(const struct taiwins_menu_item * items, const int len)
{
	struct wl_array serialized;
	serialized.alloc = 0;
	serialized.size = sizeof(struct taiwins_menu_item) * len;
	serialized.data = (void *)items;
	return serialized;
}

/* whether this is a menu item */
static bool
_lua_is_menu_item(struct lua_State *L, int idx)
{
	if (lua_rawlen(L, idx) != 2)
		return false;
	int len[2] = {TAIWINS_MAX_MENU_ITEM_NAME,
		      TAIWINS_MAX_MENU_CMD_LEN};
	for (int i = 0; i < 2; ++i) {
		lua_rawgeti(L, idx, i+1);
		const char *value = (lua_type(L, -1) == LUA_TSTRING) ?
			lua_tostring(L, -1) : NULL;
		if (value == NULL || strlen(value) >= (len[i]-1)) {
			lua_pop(L, 1);
			return false;
		}
		lua_pop(L, 1);
	}
	return true;
}

static bool
_lua_parse_menu(struct lua_State *L, vector_t *menus)
{
	bool parsed = true;
	struct taiwins_menu_item menu_item = {
		.has_submenu = false,
		.len = 0};
	if (_lua_is_menu_item(L, -1)) {
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		strop_ncpy(menu_item.endnode.title, lua_tostring(L, -2),
			TAIWINS_MAX_MENU_ITEM_NAME);
		strop_ncpy(menu_item.endnode.cmd, lua_tostring(L, -1),
			TAIWINS_MAX_MENU_CMD_LEN);
		lua_pop(L, 2);
		vector_append(menus, &menu_item);
	} else if (lua_istable(L, -1)) {
		int n = lua_rawlen(L, -1);
		int currlen = menus->len;
		for (int i = 1; i <= n && parsed; i++) {
			lua_rawgeti(L, -1, i);
			parsed = parsed && _lua_parse_menu(L, menus);
			lua_pop(L, 1);
		}
		if (parsed) {
			menu_item.has_submenu = true;
			menu_item.len = menus->len - currlen;
			vector_append(menus, &menu_item);
		}
	} else
		return false;
	return parsed;
}

static int
_lua_set_menus(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);

	vector_init_zero(&shell->menu, sizeof(struct taiwins_menu_item), NULL);
	_lua_stackcheck(L, 2);
	luaL_checktype(L, 2, LUA_TTABLE);
	if (!_lua_parse_menu(L, &shell->menu)) {
		vector_destroy(&shell->menu);
		return luaL_error(L, "error parsing menus.");
	}
	return 0;
}

static int
_lua_set_sleep_timer(lua_State *L)
{
	return 0;
}

/* compositor will lock in the given seconds, then try to sleep after another few weeks */
static int
_lua_set_lock_timer(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	_lua_stackcheck(L, 2);
	int32_t seconds = luaL_checknumber(L, 2);
	if (seconds < 0) {
		return luaL_error(L, "idle time must be a non negative integers.");
	}
	shell->lock_countdown = seconds;
	return 0;
}

static int
_lua_request_shell(lua_State *L)
{
	lua_newtable(L);
	//register global methods or fields
	luaL_getmetatable(L, METATABLE_SHELL);
	lua_setmetatable(L, -2);
	return 1;
}
/*
 * function exposed for shell.
 *
 * set theme, we may actually creates a new theme globals.
 * - set wallpaper.
 * - init widgets.
 * - set menus.
 * - panel positions (we can only give it as top and down). Panel size is determined by font size, and all that color.
 * - what else?
 */
static bool
shell_add_config_component(struct taiwins_config *c, lua_State *L,
			   struct taiwins_config_component_listener *listener)
{
	struct shell *shell = container_of(listener, struct shell, config_component);
	lua_pushlightuserdata(L, shell);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);
	//register global methods.
	//creates its own metatable
	luaL_newmetatable(L, METATABLE_SHELL);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	//TODO make all those methods overrided
	REGISTER_METHOD(L, "set_wallpaper", _lua_set_wallpaper);
	REGISTER_METHOD(L, "init_widgets", _lua_set_widgets);
	REGISTER_METHOD(L, "panel_position", _lua_set_panel_position);
	REGISTER_METHOD(L, "set_menus", _lua_set_menus);
	//now methods
	lua_pop(L, 1);
	REGISTER_METHOD(L, "shell", _lua_request_shell);
	REGISTER_METHOD(L, "lock_in", _lua_set_lock_timer);
	REGISTER_METHOD(L, "sleep_in", _lua_set_sleep_timer);

	return true;
}

static void
shell_apply_lua_config(struct taiwins_config *c, bool cleanup,
		       struct taiwins_config_component_listener *listener)
{
	struct shell *shell = container_of(listener, struct shell, config_component);
	if (cleanup)
		goto cleanup;
	if (!shell->shell_resource)
		return;
	if (shell->wallpaper_path)
		shell_post_message(shell, TW_SHELL_MSG_TYPE_WALLPAPER,
				   shell->wallpaper_path);
	if (shell->widget_path)
		shell_post_message(shell, TW_SHELL_MSG_TYPE_WIDGET,
				   shell->widget_path);
	if (shell->menu.len) {
		struct wl_array serialized =
			taiwins_menu_to_wl_array(shell->menu.elems, shell->menu.len);
		shell_post_data(shell, TW_SHELL_MSG_TYPE_MENU, &serialized);
	}
	if (shell->lock_countdown > 0) {
		shell->ec->idle_time = shell->lock_countdown;
		shell->lock_countdown = -1;
	}
	shell_send_panel_pos(shell);

cleanup:
	vector_destroy(&shell->menu);
	if (shell->wallpaper_path) {
		free((void *)shell->wallpaper_path);
		shell->wallpaper_path = NULL;
	}
	if (shell->widget_path) {
		free((void *)shell->widget_path);
		shell->widget_path = NULL;
	}
}

////////////////////////// SHELL FUNCIONS /////////////////////////////////

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
	fprintf(stderr, "shell-unbined\n");
}

static void
bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct shell *shell = data;
	uid_t uid; gid_t gid; pid_t pid;
	struct wl_resource *resource = NULL;
	struct weston_layer *layer;

	resource = wl_resource_create(client, &tw_shell_interface,
				      tw_shell_interface.version, id);

	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (shell->shell_client &&
	    (uid != shell->uid || pid != shell->pid || gid != shell->gid)) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "client %d is not un atherized shell", id);
		wl_resource_destroy(resource);
	}
	wl_list_for_each(layer, &shell->ec->layer_list, link) {
		weston_log("layer position %x\n", layer->position);
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

	//send configurations to clients now
	shell_apply_lua_config(shell->config, false, &shell->config_component);
	struct weston_output *output;
	wl_list_for_each(output, &shell->ec->output_list, link) {
		int ith_output = shell_ith_output(shell, output);
		tw_shell_send_output_configure(shell->shell_resource, ith_output,
					       output->width, output->height, output->scale,
					       ith_output == 0,
					       TW_SHELL_OUTPUT_MSG_CONNECTED);
	}
}

///////////////////////// exposed APIS ////////////////////////////////

void
shell_create_ui_elem(struct shell *shell,
		     struct wl_client *client,
		     uint32_t tw_ui,
		     struct wl_resource *wl_surface,
		     uint32_t x, uint32_t y,
		     enum tw_ui_type type)
{
	struct weston_output *output = tw_get_focused_output(shell->ec);
	create_ui_element(client, shell, NULL, tw_ui, wl_surface, output,
			  x, y, type);
}

void
shell_post_data(struct shell *shell, uint32_t type,
		struct wl_array *msg)
{
	tw_shell_send_shell_msg(shell->shell_resource,
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
	tw_shell_send_shell_msg(shell->shell_resource, type, &arr);
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
	if (shell->panel_pos == TW_SHELL_PANEL_POS_TOP)
		geo.y += shell_output->panel.binded->height;
	else
		geo.height -= shell_output->panel.binded->height;
	return geo;
}

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

static inline void
shell_init_options(struct shell *shell)
{
	vector_init_zero(&shell->menu, sizeof(struct taiwins_menu_item), NULL);
	shell->panel_pos = TW_SHELL_PANEL_POS_TOP;
	shell->lock_countdown = -1;
	shell->sleep_countdown = -1;
	shell->wallpaper_path = NULL;
	shell->widget_path = NULL;
}


/**
 * @brief announce the taiwins shell protocols.
 *
 * We should start the client at this point as well.
 */
struct shell*
announce_shell(struct weston_compositor *ec, const char *path,
	       struct taiwins_config *config)
{
	oneshell.ec = ec;
	oneshell.ready = false;
	oneshell.the_widget_surface = NULL;
	oneshell.shell_client = NULL;
	oneshell.config = config;

	//TODO leaking a wl_global
	oneshell.shell_global =  wl_global_create(ec->wl_display, &tw_shell_interface,
						  tw_shell_interface.version, &oneshell,
						  bind_shell);
	if (path) {
		assert(strlen(path) +1 <= sizeof(oneshell.path));
		strcpy(oneshell.path, path);
		struct wl_event_loop *loop = wl_display_get_event_loop(ec->wl_display);
		wl_event_loop_add_idle(loop, launch_shell_client, &oneshell);
	}
	shell_add_listeners(&oneshell);
	shell_init_options(&oneshell);

	//binding
	wl_list_init(&oneshell.add_binding.link);
	oneshell.add_binding.apply = shell_add_bindings;
	taiwins_config_add_apply_bindings(config, &oneshell.add_binding);
	//config_componenet
	wl_list_init(&oneshell.config_component.link);
	oneshell.config_component.init = shell_add_config_component;
	oneshell.config_component.apply = shell_apply_lua_config;
	taiwins_config_add_component(config, &oneshell.config_component);

	return &oneshell;
}
