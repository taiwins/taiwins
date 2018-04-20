#include <compositor.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <helpers.h>
#include <linux/input.h>

#include "taiwins.h"
#include "shell.h"

struct twshell {
	struct weston_compositor *ec;
	//you probably don't want to have the layer
	struct weston_layer background_layer;
	struct weston_layer ui_layer;

	struct weston_position panel_position;
	struct weston_position current_widget_pos;
	//the widget is the global view
	struct weston_surface *the_widget_surface;
};


static struct twshell oneshell;

struct weston_layer *
get_shell_ui_layer(void)
{
	return &oneshell.ui_layer;
}

struct weston_layer *
get_shell_background_layer(void)
{
	return &oneshell.background_layer;
}


/**
 * configure a static surface for its location, the location is determined,
 * output.x + x, output.y + y
 */
static void
setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y)
{
	//delete all the other view in the layer, rightnow we assume we only
	//have one view on the layer, this may not be true for UI layer.
	struct weston_view *v, *next;
	wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link) {
		if (v->output == view->output && v != view) {
			weston_view_unmap(v);
			v->surface->committed = NULL;
			weston_surface_set_label_func(v->surface, NULL);
		}
	}
	//the point of calling this function
	weston_view_set_position(view, view->surface->output->x + x, view->surface->output->y + y);
	view->surface->is_mapped = true;
	view->is_mapped = true;

	//for the new created view
	if (wl_list_empty(&view->layer_link.link)) {
		weston_layer_entry_insert(&layer->view_list, &view->layer_link);
		weston_compositor_schedule_repaint(view->surface->compositor);
	}
}

static void
setup_ui_view(struct weston_view *uiview, struct weston_layer *uilayer, int x, int y)
{
	//on the ui layer, we only have one view per wl_surface
	struct weston_surface *surface = uiview->surface;
	struct weston_output *output = uiview->output;

	struct weston_view *v, *next;

	wl_list_for_each_safe(v, next, &surface->views, surface_link) {
		if (v->output == uiview->output && v != uiview) {
			weston_view_unmap(v);
			v->surface->committed = NULL;
			weston_surface_set_label_func(v->surface, NULL);
		}
	}
	weston_view_set_position(uiview, output->x + x, output->y + y);
	uiview->surface->is_mapped = true;
	uiview->is_mapped = true;
	if (wl_list_empty(&uiview->layer_link.link)) {
		weston_layer_entry_insert(&uilayer->view_list, &uiview->layer_link);
		weston_compositor_schedule_repaint(surface->compositor);
	}
}

static void
widget_committed(struct weston_surface *surface, int sx, int sy)
{
	struct twshell *shell = (struct twshell *)surface->committed_private;
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	setup_ui_view(view, &shell->ui_layer, shell->current_widget_pos.x, shell->current_widget_pos.y);
}

static void
setup_shell_widget(struct wl_client *client,
		   struct wl_resource *resource,
		   struct wl_resource *surface,
		   struct wl_resource *output,
		   uint32_t x,
		   uint32_t y)
{

	struct weston_view *view, *next;
	struct weston_surface *wd_surface =
		(struct weston_surface *)wl_resource_get_user_data(surface);
	struct weston_output *ws_output = weston_output_from_resource(output);
	wd_surface->output = ws_output;
	oneshell.the_widget_surface = wd_surface;
	//this is very fake
	if (wd_surface->committed) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "surface already have a role");
		return;
	}
	wd_surface->committed = widget_committed;
	wd_surface->committed_private = &oneshell;
	wl_list_for_each_safe(view, next, &wd_surface->views, surface_link)
		weston_view_destroy(view);
	//create the view for it
	view = weston_view_create(wd_surface);
	view->output = ws_output;
	oneshell.current_widget_pos.x = x;
	oneshell.current_widget_pos.y = y;
	struct weston_seat *seat0 = wl_container_of(oneshell.ec->seat_list.next, seat0, link);
	weston_view_activate(view, seat0, WESTON_ACTIVATE_FLAG_CLICKED);
;
	//it won't work if we don't commit right?
}

static void
_hide_shell_widget(struct weston_surface *wd_surface)
{
	struct weston_view *view, *next;
	wd_surface->committed = NULL;
	wd_surface->committed_private = NULL;
	wl_list_for_each_safe(view, next, &wd_surface->views, surface_link)
		weston_view_destroy(view);
}

static void
hide_shell_widget(struct wl_client *client,
		  struct wl_resource *resource,
		  struct wl_resource *surface)
{
	struct weston_surface *wd_surface =
		(struct weston_surface *)wl_resource_get_user_data(surface);
	_hide_shell_widget(wd_surface);
}

static void
shell_widget_should_close_on_keyboard(struct weston_keyboard *keyboard,
					   uint32_t time, uint32_t key,
					   void *data)
{
	if (keyboard->focus != oneshell.the_widget_surface &&
	    oneshell.the_widget_surface)
		_hide_shell_widget(oneshell.the_widget_surface);
}

static void
shell_widget_should_close_on_cursor(struct weston_pointer *pointer,
				    uint32_t time, uint32_t button,
				    void *data)
{
	struct weston_view *view = pointer->focus;
	struct weston_surface *surface = view->surface;
	if (surface != oneshell.the_widget_surface &&
		oneshell.the_widget_surface)
		_hide_shell_widget(oneshell.the_widget_surface);
}



static void
background_committed(struct weston_surface *surface, int sx, int sy)
{
	struct twshell *shell = (struct twshell *)surface->committed_private;
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	setup_static_view(view, &shell->background_layer, 0, 0);
}


static void
set_background(struct wl_client *client,
	       struct wl_resource *resource,
	       struct wl_resource *output,
	       struct wl_resource *surface)
{
	struct weston_surface *bg_surface =
		(struct weston_surface *)wl_resource_get_user_data(surface);
	struct weston_view *view, *next;

	if (bg_surface->committed) {
		wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT, "surface already has a role");
		return;
	}
	bg_surface->committed = background_committed;
	bg_surface->committed_private = &oneshell;
	//this should be alright
	wl_list_for_each_safe(view, next, &bg_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(bg_surface);
	bg_surface->output = weston_output_from_resource(output);
	view->output = bg_surface->output;
	taiwins_shell_send_configure(resource, surface, bg_surface->output->scale, 0,
				     bg_surface->output->width, bg_surface->output->height);
}

static void
panel_committed(struct weston_surface *surface, int sx, int sy)
{
	//now by default, we just put it on the top left
	struct twshell *shell = (struct twshell *)surface->committed_private;
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	setup_ui_view(view, &shell->ui_layer, 0, 0);
//	setup_static_view(view, &shell->ui_layer, 0, 0);
}


static void
set_panel(struct wl_client *client,
	  struct wl_resource *resource,
	  struct wl_resource *output,
	  struct wl_resource *surface)
{
	//this should be replace by weston_surface_from_resource
	struct weston_surface *pn_surface =
		(struct weston_surface *)wl_resource_get_user_data(surface);
	struct weston_view *view, *next;

	if (pn_surface->committed) {
		wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT, "surface already has a role");
		return;
	}
	pn_surface->committed = panel_committed;
	pn_surface->committed_private = &oneshell;
	//destroy all the views, I am not sure if right
	wl_list_for_each_safe(view, next, &pn_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(pn_surface);
	pn_surface->output = weston_output_from_resource(output);
	view->output = weston_output_from_resource(output);
	//TODO user configure panel size
	taiwins_shell_send_configure(resource, surface, pn_surface->output->scale, 0,
				     pn_surface->output->width, 16);

}


static struct taiwins_shell_interface shell_impl = {
	.set_background = set_background,
	.set_panel = set_panel,
	.set_widget = setup_shell_widget,
	.hide_widget = hide_shell_widget,
};

static void
unbind_shell(struct wl_resource *resource)
{

}

static void
bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client, &taiwins_shell_interface,
							  TWSHELL_VERSION, id);
	//TODO verify the client that from which is from wl_client_create, but rightnow we just do this
	wl_resource_set_implementation(resource, &shell_impl, data, unbind_shell);
//	wl_resource_add_destroy_listener(resource, NULL);
}


void
add_shell_bindings(struct weston_compositor *ec)
{
	weston_compositor_add_key_binding(ec, KEY_ESC, 0, shell_widget_should_close_on_keyboard, NULL);
	weston_compositor_add_button_binding(ec, BTN_LEFT, 0, shell_widget_should_close_on_cursor, NULL);
	weston_compositor_add_button_binding(ec, BTN_RIGHT, 0, shell_widget_should_close_on_cursor, NULL);
}

void
announce_shell(struct weston_compositor *ec)
{
	//I can make the static shell
	weston_layer_init(&oneshell.background_layer, ec);
	weston_layer_set_position(&oneshell.background_layer, WESTON_LAYER_POSITION_BACKGROUND);
	oneshell.the_widget_surface = NULL;
	oneshell.ec = ec;

	weston_layer_init(&oneshell.ui_layer, ec);
	weston_layer_set_position(&oneshell.ui_layer, WESTON_LAYER_POSITION_UI);

	wl_global_create(ec->wl_display, &taiwins_shell_interface, TWSHELL_VERSION, &oneshell, bind_shell);
	add_shell_bindings(ec);
}
