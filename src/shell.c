#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <helpers.h>
#include <time.h>
#include <linux/input.h>

#include "taiwins.h"
#include "shell.h"

struct twshell {
	int pid, uid, gid;
	struct weston_compositor *ec;
	//you probably don't want to have the layer
	struct weston_layer background_layer;
	struct weston_layer ui_layer;

	//the widget is the global view
	struct weston_surface *the_widget_surface;
	bool ready;
};

static struct twshell oneshell;

/* destroy info */
struct view_pos_info {
	int32_t x;
	int32_t y;
	struct wl_listener destroy_listener;
	struct twshell *shell;
};

void view_pos_destroy(struct wl_listener *listener, void *data)
{
	//we have to do with that since the data is weston_view
	free(container_of(listener, struct view_pos_info, destroy_listener));
}


enum twshell_view_t {
	twshell_view_UNIQUE, /* like the background */
	twshell_view_STATIC, /* like the ui element */
};


static void
setup_view(struct weston_view *view, struct weston_layer *layer,
	   int x, int y, enum twshell_view_t type)
{
	struct weston_surface *surface = view->surface;
	struct weston_output *output = view->output;

	struct weston_view *v, *next;
	if (type == twshell_view_UNIQUE) {
	//view like background, only one is allowed in the layer
		wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link)
			if (v->output == view->output && v != view) {
				//unmap does the list removal
				weston_view_unmap(v);
				v->surface->committed = NULL;
				weston_surface_set_label_func(surface, NULL);
			}
	}
	else if (type == twshell_view_STATIC) {
		//element
		wl_list_for_each_safe(v, next, &surface->views, surface_link) {
			if (v->output == view->output && v != view) {
				weston_view_unmap(v);
				v->surface->committed = NULL;
				weston_surface_set_label_func(v->surface, NULL);
			}
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
	struct view_pos_info *pos_info = surface->committed_private;
	struct twshell *shell = pos_info->shell;
	//get the first view, as ui element has only one view
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	//it is not true for both
	setup_view(view, &shell->ui_layer, pos_info->x, pos_info->y, twshell_view_UNIQUE);
}

/**
 * @brief the effort to make unified set_surface
 */
static bool
_setup_surface(struct twshell *shell,
		 struct wl_resource *wl_surface, struct wl_resource *wl_output,
		 struct wl_resource *wl_resource,
		 void (*committed)(struct weston_surface *, int32_t, int32_t),
		 int32_t x, int32_t y)
{
	struct weston_surface *surface = weston_surface_from_resource(wl_surface);
	struct weston_output *output = weston_output_from_resource(wl_output);

}

static bool
set_surface(struct twshell *shell,
	    struct weston_surface *surface, struct weston_output *output,
	    struct wl_resource *wl_resource,
	    void (*committed)(struct weston_surface *, int32_t, int32_t),
	    int32_t x, int32_t y)
{
	struct weston_view *view, *next;

	if (surface->committed) {
		wl_resource_post_error(wl_resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface already have a role");
		return false;
	}
	wl_list_for_each_safe(view, next, &surface->views, surface_link) {
		weston_view_destroy(view);
	}
	view = weston_view_create(surface);
	//temporary code, change to slot alloctor instead
	struct view_pos_info *pos_info = malloc(sizeof(struct view_pos_info));
	pos_info->x = x;
	pos_info->y = y;
	pos_info->shell = shell;
	wl_list_init(&pos_info->destroy_listener.link);
	pos_info->destroy_listener.notify = view_pos_destroy;
	surface->committed = committed;
	surface->committed_private = pos_info;
	wl_signal_add(&view->destroy_signal, &pos_info->destroy_listener);
	surface->output = output;
	view->output = output;
	return true;


}



static void
commit_ui_surface(struct weston_surface *surface, int sx, int sy)
{
	//the sx and sy are from attach or attach_buffer attach sets pending
	//state, when commit request triggered, pending state calls
	//weston_surface_state_commit to use the sx, and sy in here
	//the confusion is that we cannot use sx and sy directly almost all the time.
	struct view_pos_info *pos_info = surface->committed_private;
	struct twshell *shell = pos_info->shell;
	//get the first view, as ui element has only one view
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	//it is not true for both
	setup_view(view, &shell->ui_layer, pos_info->x, pos_info->y, twshell_view_STATIC);
}




static void
shell_widget_should_close_on_keyboard(struct weston_keyboard *keyboard,
				      const struct timespec *time, uint32_t key,
				      void *data)
{
	if (keyboard->focus != oneshell.the_widget_surface &&
	    oneshell.the_widget_surface)
		twshell_close_ui_surface(oneshell.the_widget_surface);
}

static void
shell_widget_should_close_on_cursor(struct weston_pointer *pointer,
				    const struct timespec *time, uint32_t button,
				    void *data)
{
	struct weston_view *view = pointer->focus;
	struct weston_surface *surface = view->surface;
	if (surface != oneshell.the_widget_surface &&
		oneshell.the_widget_surface)
		twshell_close_ui_surface(oneshell.the_widget_surface);
}



/////////////////////////// Taiwins shell interface //////////////////////////////////

static void
close_widget(struct wl_client *client,
		  struct wl_resource *resource,
		  struct wl_resource *surface)
{
	struct weston_surface *wd_surface =
		(struct weston_surface *)wl_resource_get_user_data(surface);
	twshell_close_ui_surface(wd_surface);
}

static void
set_widget(struct wl_client *client,
		   struct wl_resource *resource,
		   struct wl_resource *wl_surface,
		   struct wl_resource *wl_output,
		   uint32_t x,
		   uint32_t y)
{
	struct twshell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface = weston_surface_from_resource(wl_surface);
	struct weston_output *output = weston_output_from_resource(wl_output);
	set_surface(shell, surface, output, resource, commit_background, 0, 0);

	/* _setup_surface(&oneshell, surface, output, resource, commit_ui_surface, x, y); */
	struct weston_seat *seat0 = weston_get_default_seat(oneshell.ec);
	struct weston_view *view = weston_default_view_from_surface(surface);
	/* fprintf(stderr, "the view is %p\n", view); */
	weston_view_activate(view, seat0, WESTON_ACTIVATE_FLAG_CLICKED);
}


static void
set_background(struct wl_client *client,
	       struct wl_resource *resource,
	       struct wl_resource *wl_output,
	       struct wl_resource *wl_surface)
{
	struct twshell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface = weston_surface_from_resource(wl_surface);
	struct weston_output *output = weston_output_from_resource(wl_output);
	set_surface(shell, surface, output, resource, commit_background, 0, 0);

//	_setup_surface(&oneshell, surface, output, resource, commit_backgrand, 0, 0);
	taiwins_shell_send_configure(resource, wl_surface, output->scale, 0,
				     output->width, output->height);
}

static void
set_panel(struct wl_client *client,
	  struct wl_resource *resource,
	  struct wl_resource *wl_output,
	  struct wl_resource *wl_surface)
{
	struct twshell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface = weston_surface_from_resource(wl_surface);
	struct weston_output *output = weston_output_from_resource(wl_output);
	set_surface(shell, surface, output, resource, commit_ui_surface, 0, 0);

//	_setup_surface(&oneshell, surface, output, resource, commit_ui_surface, 0, 0);
	taiwins_shell_send_configure(resource, wl_surface, output->scale, 0, //edge
				     output->width, 16);

}


static struct taiwins_shell_interface shell_impl = {
	.set_background = set_background,
	.set_panel = set_panel,
	.set_widget = set_widget,
	.hide_widget = close_widget,
};

/////////////////////////// twshell global ////////////////////////////////



static void
unbind_twshell(struct wl_resource *resource)
{
	struct weston_view *v, *n;

	struct twshell *shell = wl_resource_get_user_data(resource);
	weston_layer_unset_position(&shell->background_layer);
	weston_layer_unset_position(&shell->ui_layer);

	wl_list_for_each_safe(v, n, &shell->background_layer.view_list.link, layer_link.link)
		weston_view_unmap(v);
	wl_list_for_each_safe(v, n, &shell->ui_layer.view_list.link, layer_link.link)
		weston_view_unmap(v);
	//TODO maybe destroy the surface as well.
}

static void
bind_twshell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct twshell *shell = data;
	struct wl_resource *resource =
		wl_resource_create(client, &taiwins_shell_interface,
				   taiwins_shell_interface.version, id);
	/* client verification code */
	/* int uid, pid, gid; */
	/* wl_client_get_credentials(client, &pid, &uid, &gid); */
	/* if (uid != shell->uid || pid != shell->pid || gid != shell->gid) { */
	/*	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, */
	/*			       "client %d is not un atherized shell", id); */
	/*	wl_resource_destroy(resource); */
	/* } */
	wl_resource_set_implementation(resource, &shell_impl, shell, unbind_twshell);
	shell->ready = true;
}


static void
end_twshell(struct wl_listener *listener, void *data)
{
	//move the code to the unbind_shell, this is not useful anyway
	struct twshell *shell = &oneshell;
	weston_layer_unset_position(&shell->background_layer);
	weston_layer_unset_position(&shell->ui_layer);

	//TODO probably remove shell application resources: you cannot force
	//children to remove it, it has too many requests. So I have to figure
	//out about that, otherwise, we will need to wait untill all children quit
}


static struct wl_listener twshell_destructor = {
	.link = {
		.prev = &twshell_destructor.link,
		.next = &twshell_destructor.link
	},
	.notify = end_twshell
};

///////////////////////// exposed APIS ////////////////////////////////


/**
 * @brief make the ui surface appear
 *
 * using the UI_LAYER for for seting up the the view
 */
bool
twshell_set_ui_surface(struct twshell *shell, struct weston_surface *surface, struct weston_output *output,
		       struct wl_resource *wl_resource,
		       struct wl_listener *surface_destroy_listener,
		       int32_t x, int32_t y)
{
	//TODO destroy signal, I am not sure how the wl_surface's destroy signal is called.
	//clearly the weston_desktop_surface destroy signal is
	if (surface_destroy_listener) {
		wl_signal_add(&surface->destroy_signal, surface_destroy_listener);
	}
	return set_surface(shell, surface, output, wl_resource, commit_ui_surface, x, y);
//	return _setup_surface(shell, wl_surface, wl_output, wl_resource, commit_ui_surface, x, y);
}

void
twshell_close_ui_surface(struct weston_surface *wd_surface)
{
	struct weston_view *view, *next;
	wd_surface->committed = NULL;
	wl_list_for_each_safe(view, next, &wd_surface->views, surface_link)
		weston_view_destroy(view);
	//make it here so we call the free first
	wd_surface->committed_private = NULL;
}



void
add_shell_bindings(struct weston_compositor *ec)
{
	weston_compositor_add_key_binding(ec, KEY_ESC, 0, shell_widget_should_close_on_keyboard, NULL);
	weston_compositor_add_button_binding(ec, BTN_LEFT, 0, shell_widget_should_close_on_cursor, NULL);
	weston_compositor_add_button_binding(ec, BTN_RIGHT, 0, shell_widget_should_close_on_cursor, NULL);
}

struct twshell*
announce_twshell(struct weston_compositor *ec)
{
	oneshell.the_widget_surface = NULL;
	oneshell.ec = ec;
	oneshell.ready = false;

	weston_layer_init(&oneshell.background_layer, ec);
	weston_layer_set_position(&oneshell.background_layer, WESTON_LAYER_POSITION_BACKGROUND);
	weston_layer_init(&oneshell.ui_layer, ec);
	weston_layer_set_position(&oneshell.ui_layer, WESTON_LAYER_POSITION_UI);

	wl_global_create(ec->wl_display, &taiwins_shell_interface,
			 taiwins_shell_interface.version, &oneshell, bind_twshell);
	add_shell_bindings(ec);
	wl_signal_add(&ec->destroy_signal, &twshell_destructor);
	//TODO here we need to launch the taiwins-shell client

	return &oneshell;
}
