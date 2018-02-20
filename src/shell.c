#include <compositor.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <helpers.h>

#include "weston.h"
#include "shell.h"

struct twshell {
	struct weston_compositor *ec;
	struct weston_layer background_layer;
	struct weston_layer ui_layer;
};

static struct twshell oneshell;

/**
 * configure a static surface for its location, the location is determined,
 * output.x + x, output.y + y
 */
void
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
	setup_static_view(view, &shell->ui_layer, 0, 0);
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
}



void
announce_shell(struct weston_compositor *ec)
{
	//I can make the static shell
	weston_layer_init(&oneshell.background_layer, ec);
	weston_layer_set_position(&oneshell.background_layer, WESTON_LAYER_POSITION_BACKGROUND);

	weston_layer_init(&oneshell.ui_layer, ec);
	weston_layer_set_position(&oneshell.ui_layer, WESTON_LAYER_POSITION_UI);


	wl_global_create(ec->wl_display, &taiwins_shell_interface, TWSHELL_VERSION, &oneshell, bind_shell);
}
