#include <string.h>
#include <assert.h>
#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-desktop-server-protocol.h>
#include <helpers.h>
#include <time.h>
#include <linux/input.h>

#include "taiwins.h"
#include "shell.h"
#include "bindings.h"

struct shell_ui {
	struct wl_resource *resource;
	struct weston_surface *binded;
	struct weston_binding *lose_keyboard;
	struct weston_binding *lose_pointer;
	struct weston_binding *lose_touch;
	uint32_t x; uint32_t y;
	struct weston_layer *layer;
};


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
	free(ui_elem);
}

static struct shell_ui *
shell_ui_create_with_binding(struct wl_resource *tw_ui, struct weston_surface *s)
{
	struct weston_compositor *ec = s->compositor;
	struct shell_ui *ui = malloc(sizeof(struct shell_ui));
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
	return ui;
err_bind_touch:
	weston_binding_destroy(p);
err_bind_ptr:
	weston_binding_destroy(k);
err_bind_keyboard:
	free(ui);
err_ui_create:
	return NULL;
}

static struct shell_ui *
shell_ui_create_simple(struct wl_resource *tw_ui, struct weston_surface *s)
{
	struct shell_ui *ui = calloc(1, sizeof(struct shell_ui));
	ui->resource = tw_ui;
	ui->binded = s;
	return ui;
}

struct shell {
	uid_t uid; gid_t gid; pid_t pid;
	char path[256];
	struct wl_client *shell_client;
	struct wl_global *shell_global;

	struct weston_compositor *ec;
	//you probably don't want to have the layer
	struct weston_layer background_layer;
	struct weston_layer ui_layer;

	//the widget is the global view
	struct weston_surface *the_widget_surface;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	bool ready;

	struct {
		struct wl_global *global;
		struct weston_output *output;
	} tw_outputs[16];

};

static struct shell oneshell;


/************** output created ********************/
static inline size_t
shell_n_outputs(struct shell *shell)
{
	for (int i = 0; i < 16; i++) {
		if (shell->tw_outputs[i].global == NULL &&
		    shell->tw_outputs[i].output == NULL)
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

void
bind_tw_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct weston_output *output = data;
	struct wl_resource *resource =
		wl_resource_create(client, &tw_output_interface,
				   tw_output_interface.version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	//currently there is no event we need to handle
	wl_resource_set_implementation(resource, NULL, output, NULL);
	tw_output_send_configure(resource,
				 output->width, output->height,
				 output->scale,
				 output->x, output->y, true);
}


static void
shell_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct shell *shell = container_of(listener, struct shell, output_create_listener);
	size_t ith_output = shell_n_outputs(shell);
	//if we have 16 output...
	if (ith_output == 16)
		return;
	shell->tw_outputs[ith_output].output = output;
	shell->tw_outputs[ith_output].global =
		wl_global_create(shell->ec->wl_display, &tw_output_interface,
				 tw_output_interface.version, output,
				 bind_tw_output);
	//reset back if no global is created
	if (!shell->tw_outputs[ith_output].global)
		shell->tw_outputs[ith_output].output = NULL;
}

static void
shell_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct shell *shell = container_of(listener, struct shell, output_destroy_listener);
	int i = shell_ith_output(shell, output);
	if (i < 0)
		return;
	struct wl_global *global = shell->tw_outputs[i].global;
	wl_global_destroy(global);
	shell->tw_outputs[i].global = NULL;
	shell->tw_outputs[i].output = NULL;
}
/************** output created ********************/


enum shell_view_t {
	shell_view_UNIQUE, /* like the background */
	shell_view_STATIC, /* like the ui element */
};


static void
setup_view(struct weston_view *view, struct weston_layer *layer,
	   int x, int y, enum shell_view_t type)
{
	struct weston_surface *surface = view->surface;
	struct weston_output *output = view->output;

	struct weston_view *v, *next;
	if (type == shell_view_UNIQUE) {
	//view like background, only one is allowed in the layer
		wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link)
			if (v->output == view->output && v != view) {
				//unmap does the list removal
				weston_view_unmap(v);
				v->surface->committed = NULL;
				weston_surface_set_label_func(surface, NULL);
			}
	}
	else if (type == shell_view_STATIC) {
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
	struct shell_ui *ui = surface->committed_private;
	//get the first view, as ui element has only one view
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	//it is not true for both
	if (surface->buffer_ref.buffer)
		setup_view(view, ui->layer, ui->x, ui->y, shell_view_UNIQUE);
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
		setup_view(view, ui->layer, ui->x, ui->y, shell_view_STATIC);
}

static bool
set_surface(struct shell *shell,
	    struct weston_surface *surface, struct weston_output *output,
	    struct wl_resource *wl_resource,
	    void (*committed)(struct weston_surface *, int32_t, int32_t),
	    int32_t x, int32_t y)
{
	//TODO, use wl_resource_get_user_data for position
	struct weston_view *view, *next;
	struct shell_ui *ui = wl_resource_get_user_data(wl_resource);
	ui->x = x; ui->y = y;

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




/////////////////////////// Taiwins shell interface //////////////////////////////////

static void
create_ui_element(struct wl_client *client,
		  struct shell *shell,
		  uint32_t tw_ui,
		  struct wl_resource *wl_surface,
		  struct wl_resource *tw_output,
		  uint32_t x, uint32_t y,
		  enum tw_ui_type type)
{
	struct weston_output *output = (tw_output) ?
		wl_resource_get_user_data(tw_output) :
		tw_get_focused_output(shell->ec);
	struct weston_seat *seat = tw_get_default_seat(shell->ec);

	struct weston_surface *surface = tw_surface_from_resource(wl_surface);
	weston_seat_set_keyboard_focus(seat, surface);
	struct wl_resource *tw_ui_resource = wl_resource_create(client, &tw_ui_interface, 1, tw_ui);
	if (!tw_ui_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	struct shell_ui *elem = (type == TW_UI_TYPE_WIDGET) ?
		shell_ui_create_with_binding(tw_ui_resource, surface) :
		shell_ui_create_simple(tw_ui_resource, surface);


	wl_resource_set_implementation(tw_ui_resource, NULL, elem, shell_ui_unbind);
	elem->x = x;
	elem->y = y;

	switch (type) {
	case TW_UI_TYPE_PANEL:
		tw_ui_send_configure(tw_ui_resource, output->width, 32);
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_ui_surface, x, y);
		break;
	case TW_UI_TYPE_BACKGROUND:
		tw_ui_send_configure(tw_ui_resource, output->width, output->height);
		elem->layer = &shell->background_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_background, x, y);
		break;
	case TW_UI_TYPE_WIDGET:
		elem->layer = &shell->ui_layer;
		set_surface(shell, surface, output, tw_ui_resource, commit_ui_surface, x, y);
		break;
	}

}

static void
create_shell_panel(struct wl_client *client,
		   struct wl_resource *resource,
		   uint32_t tw_ui,
		   struct wl_resource *wl_surface,
		   struct wl_resource *tw_output)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	create_ui_element(client, shell, tw_ui, wl_surface, tw_output,
			  0, 0, TW_UI_TYPE_PANEL);
}

static void
launch_shell_widget(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t tw_ui,
		    struct wl_resource *wl_surface,
		    struct wl_resource *tw_output,
		    uint32_t x, uint32_t y)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	create_ui_element(client, shell, tw_ui, wl_surface, tw_output,
			  x, y, TW_UI_TYPE_WIDGET);
}

static void
create_shell_background(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t tw_ui,
			struct wl_resource *wl_surface,
			struct wl_resource *tw_output)
{
	struct shell *shell = wl_resource_get_user_data(resource);
	create_ui_element(client, shell, tw_ui, wl_surface, tw_output,
			  0, 0, TW_UI_TYPE_BACKGROUND);

}

static struct tw_shell_interface shell_impl = {
	.create_panel = create_shell_panel,
	.create_background = create_shell_background,
	.launch_widget = launch_shell_widget,
};

/////////////////////////// shell global ////////////////////////////////


static void
unbind_shell(struct wl_resource *resource)
{
	struct weston_view *v, *n;

	struct shell *shell = wl_resource_get_user_data(resource);
	weston_layer_unset_position(&shell->background_layer);
	weston_layer_unset_position(&shell->ui_layer);

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
	struct wl_resource *resource =
		wl_resource_create(client, &tw_shell_interface,
				   tw_shell_interface.version, id);

	uid_t uid; gid_t gid; pid_t pid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (shell->shell_client &&
	    (uid != shell->uid || pid != shell->pid || gid != shell->gid)) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "client %d is not un atherized shell", id);
		wl_resource_destroy(resource);
	}
	struct weston_layer *layer;
	wl_list_for_each(layer, &shell->ec->layer_list, link) {
		fprintf(stderr, "layer position %x\n", layer->position);
	}
	//only add the layers if we have a shell.
	weston_layer_init(&shell->background_layer, shell->ec);
	weston_layer_set_position(&shell->background_layer, WESTON_LAYER_POSITION_BACKGROUND);
	weston_layer_init(&shell->ui_layer, shell->ec);
	weston_layer_set_position(&shell->ui_layer, WESTON_LAYER_POSITION_UI);

	wl_resource_set_implementation(resource, &shell_impl, shell, unbind_shell);
	shell->ready = true;
}

///////////////////////// exposed APIS ////////////////////////////////


void
shell_create_ui_elem(struct shell *shell,
		       struct wl_client *client,
		       uint32_t tw_ui,
		       struct wl_resource *wl_surface,
		       struct wl_resource *tw_output,
		       uint32_t x, uint32_t y,
		       enum tw_ui_type type)
{
	create_ui_element(client, shell, tw_ui, wl_surface, tw_output, x, y, type);
}


static void
launch_shell_client(void *data)
{
	struct shell *shell = data;
	shell->shell_client = tw_launch_client(shell->ec, shell->path);
	wl_client_get_credentials(shell->shell_client, &shell->pid, &shell->uid, &shell->gid);
}

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


void
shell_add_bindings(struct shell *shell,
		   struct tw_bindings *bindings)
{
	//the lookup binding
	struct tw_axis_motion motion;
	//add the zoom binding
	motion.modifier = MODIFIER_CTRL | MODIFIER_SUPER;
	motion.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL;
	tw_bindings_add_axis(bindings, &motion, zoom_axis, shell);
}


/**
 * @brief announce the taiwins shell protocols.
 *
 * We should start the client at this point as well.
 */
struct shell*
announce_shell(struct weston_compositor *ec, const char *path)
{
	oneshell.ec = ec;
	oneshell.ready = false;
	oneshell.the_widget_surface = NULL;
	oneshell.shell_client = NULL;

	//TODO leaking a wl_global
	oneshell.shell_global =  wl_global_create(ec->wl_display, &tw_shell_interface,
						  tw_shell_interface.version, &oneshell,
						  bind_shell);
	//we don't use the destroy signal here anymore, shell_should be
	//destroyed in the resource destructor
	//wl_signal_add(&ec->destroy_signal, &shell_destructor);
	if (path) {
		assert(strlen(path) +1 <= sizeof(oneshell.path));
		strcpy(oneshell.path, path);
		struct wl_event_loop *loop = wl_display_get_event_loop(ec->wl_display);
		wl_event_loop_add_idle(loop, launch_shell_client, &oneshell);
	}

	{
		wl_list_init(&oneshell.output_create_listener.link);
		oneshell.output_create_listener.notify = shell_output_created;
		wl_list_init(&oneshell.output_destroy_listener.link);
		oneshell.output_destroy_listener.notify = shell_output_destroyed;
		wl_signal_add(&ec->output_created_signal, &oneshell.output_create_listener);
		wl_signal_add(&ec->output_destroyed_signal, &oneshell.output_destroy_listener);

		struct weston_output *output;
		wl_list_for_each(output, &ec->output_list, link)
			shell_output_created(&oneshell.output_create_listener, output);
	}

	return &oneshell;
}
