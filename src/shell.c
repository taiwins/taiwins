#include <compositor.h>
#include <wayland-taiwins-shell-server-protocol.h>


struct twshell {
	struct weston_layer background_layer;
	struct weston_layer command_layer;
};

static struct twshell oneshell;

/**
 * configure a static surface, it's position is determined, output.x + x, output.y + y
 */
void
setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y)
{
	weston_view_set_position(view, view->surface->output->x + x, view->surface->output->y + y);
}

void
twshell_set_background(struct wl_client *client,
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

	wl_list_for_each_safe(view, next, &bg_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(bg_surface);
	bg_surface->output = weston_output_from_resource(output);
	weston_view_set_position(view, bg_surface->output->x+0, bg_surface->output->y+0);
	weston_layer_entry_insert(&oneshell.background_layer.view_list, &view->layer_link);
	//this code is better in commit,
	bg_surface->is_mapped = true;
	//okay, now you need to setup commit and stuff

}


static struct taiwins_shell_interface shell_surface = {
	.set_background = twshell_set_background
};

void bind_shell(struct weston_compositor *ec)
{
	weston_layer_init(&oneshell.background_layer, ec);
	weston_layer_set_position(&oneshell.background_layer, WESTON_LAYER_POSITION_BACKGROUND);

	weston_layer_init(&oneshell.command_layer, ec);
	weston_layer_set_position(&oneshell.command_layer, WESTON_LAYER_POSITION_UI);
}
