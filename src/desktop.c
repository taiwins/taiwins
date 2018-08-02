#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>



#include "taiwins.h"
#include "desktop.h"

struct workspace;


/**
 * this struct handles the request and sends the event from
 * taiwins_launcher.
 */
struct launcher {
	struct weston_compositor *compositor;
	struct twshell *shell;
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_resource *resource;
	struct wl_listener close_listener;
	struct wl_resource *callback;
	unsigned int exec_id;
};

struct twdesktop {
	struct weston_compositor *compositor;
	//taiwins_launcher
	struct launcher launcher;
	/* managing current status */
	struct workspace *actived_workspace[2];
	//we may need a hidden layer
	vector_t workspaces;

	struct weston_desktop *api;

	struct wl_listener destroy_listener;
};
static struct twdesktop onedesktop;



struct workspace {
	/* a workspace have three layers,
	 * - the hiden layer that you won't be able to see, because it is covered by
	 shown float but we don't insert the third layer to
	 * the compositors since they are hiden for floating views. The postions
	 * of the two layers change when user interact with windows.
	 */
	struct weston_layer hiden_float_layout;
	struct weston_layer tiled_layout;
	struct weston_layer shown_float_layout;
	/* also, we need to remember the position of the layer */
	uint32_t float_layer_pos;
	uint32_t tiled_layer_pos;
	//the wayland-buffer
};


static void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	weston_layer_init(&wp->tiled_layout, compositor);
	weston_layer_init(&wp->shown_float_layout, compositor);
	weston_layer_init(&wp->hiden_float_layout, compositor);
	wp->float_layer_pos = WESTON_LAYER_POSITION_NORMAL + 1;
	wp->tiled_layer_pos = WESTON_LAYER_POSITION_NORMAL;
}


static void
switch_workspace(struct twdesktop *d, struct workspace *to)
{
	struct workspace *wp = d->actived_workspace[0];
	weston_layer_unset_position(&wp->tiled_layout);
	weston_layer_unset_position(&wp->shown_float_layout);

	d->actived_workspace[1] = wp;
	d->actived_workspace[0] = to;
//	weston_layer_set_position(&wp->hiden_float_layout, WESTON_LAYER_POSITION_NORMAL-1);
	weston_layer_set_position(&wp->tiled_layout, wp->tiled_layer_pos);
	weston_layer_set_position(&wp->shown_float_layout , wp->float_layer_pos);
	weston_compositor_schedule_repaint(d->compositor);
}

static void
switch_to_recent_workspace(struct twdesktop *d)
{
	struct workspace *wp = d->actived_workspace[1];
	switch_workspace(d, wp);
}

/**
 * @brief switch the tiling and floating layer.
 *
 * This happens when we focus on the different layer. Later on I can write the
 * renderer myself and blured out window that is not focused (only in the
 * application layer).
 *
 * other things need to notice here, if we are change from tiled layer to
 * floating layer, the floating layer should actually just have one view, the
 * rest should moved to hiden layer, there are some code here need to be done
 */
static bool
switch_layer(struct twdesktop *d)
{
	struct workspace *wp = d->actived_workspace[0];

	wp->float_layer_pos = wp->tiled_layout.position;
	wp->tiled_layer_pos = wp->shown_float_layout.position;

	weston_layer_unset_position(&wp->shown_float_layout);
	weston_layer_unset_position(&wp->tiled_layout);
	weston_layer_set_position(&wp->shown_float_layout, wp->float_layer_pos);
	weston_layer_set_position(&wp->tiled_layout, wp->tiled_layer_pos);
	weston_compositor_schedule_repaint(d->compositor);
	return true;
}

static void
switch_layer_refresh(struct twdesktop *d)
{
	switch_layer(d);
	weston_compositor_schedule_repaint(d->compositor);
}


/**
 * @brief the libweston-desktop implementation
 */
static void
twdesk_surface_added(struct weston_desktop_surface *surface,
	      void *user_data)
{
	fprintf(stderr, "new surface added\n");
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);

	struct weston_view *wt_view = weston_desktop_surface_create_view(surface);
	//yep, I don't think we have a output
	wt_view->is_mapped = true;
	wt_surface->is_mapped = true;
	//I am not sure if I need the output
//	weston_view_set_position(wt_view, 0, 0);

//	wl_list_init(struct wl_list *list)
	//decide ou se trouve le surface 1) tiled_layer, 2) float layer. If the
	//tiling layer, you will need to allocate the position to the
	//surface. If the floating layer, You can skip the allocator

	//And finally, switch the layer if you need to.

	//now you need to create the view

	//create
}

static void
twdesk_surface_removed(struct weston_desktop_surface *surface,
		void *user_data)
{
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	weston_surface_unmap(wt_surface);
}


static void
twdesk_surface_committed(struct weston_desktop_surface *desktop_surface,
			 int32_t sx, int32_t sy, void *data)
{
	struct workspace *wsp = onedesktop.actived_workspace[0];
	weston_layer_entry_insert(&wsp->shown_float_layout.view_list, &wt_view->layer_link);
	fprintf(stderr, "committed\n");
	weston_desktop_surface_set_activated(desktop_surface, true);
	struct weston_surface *surface =  weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	weston_view_set_position(view, 0, 0);
	struct weston_seat *active_seat = container_of(onedesktop.compositor->seat_list.next, struct weston_seat, link);
	struct weston_keyboard *keyboard = active_seat->keyboard_state;
	weston_keyboard_set_focus(keyboard, surface);

	weston_view_schedule_repaint(view);

}

//doesn't seems to work!!!
static struct weston_desktop_api desktop_impl =  {
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.struct_size = sizeof(struct weston_desktop_api),
};
/*** libweston-desktop implementation ***/


static void
_free_workspace(void *data)
{
	struct weston_layer *workspace = (struct weston_layer *)data;
	while(workspace->view_list.link.next != &workspace->view_list.link) {
		struct wl_list *link = workspace->view_list.link.next;
		struct weston_view *view = container_of(link, struct weston_view, layer_link.link);
		struct weston_surface *surf = view->surface;
		weston_surface_destroy(surf);
		//close this application!!!! By close the surface?
		//we could get into a segment fault for this
	}
}


static void
set_launcher(struct wl_client *client, struct wl_resource *resource,
	     struct wl_resource *wl_surface,
	     uint32_t exec_callback, uint32_t exec_id)
{
	struct launcher *lch = wl_resource_get_user_data(resource);
	lch->surface = tw_surface_from_resource(wl_surface);
	lch->callback = wl_resource_create(client, &wl_callback_interface, 1, exec_callback);
	lch->exec_id = exec_id;

	twshell_set_ui_surface(lch->shell, lch->surface,
			       tw_get_default_output(lch->compositor),
			       resource, 100, 100);
}

static void
close_launcher_notify(struct wl_listener *listener, void *data)
{
	struct launcher *lch = container_of(listener, struct launcher, close_listener);
	twshell_close_ui_surface(lch->surface);
	wl_list_remove(&lch->close_listener.link);
}


static void
close_launcher(struct wl_client *client, struct wl_resource *resource,
	       struct wl_resource *wl_buffer)
{
	struct launcher *lch = (struct launcher *)wl_resource_get_user_data(resource);
	lch->decision_buffer = wl_shm_buffer_get(wl_buffer);
	struct weston_output *output = lch->surface->output;
	wl_signal_add(&output->frame_signal, &lch->close_listener);

	wl_callback_send_done(lch->callback, lch->exec_id);
	wl_resource_destroy(lch->callback);
}


static struct taiwins_launcher_interface launcher_impl = {
	.set_launcher = set_launcher,
	.submit = close_launcher
};


static void
unbind_desktop(struct wl_resource *r)
{
	//we should do our clean up here
}


static void
bind_desktop(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	/* int pid, uid, gid; */
	/* wl_client_get_credentials(client, &pid, &uid, &gid); */
	struct twdesktop *desktop  = data;
	struct wl_resource *wl_resource = wl_resource_create(client, &taiwins_launcher_interface,
							  TWDESKP_VERSION, id);
	desktop->launcher.resource = wl_resource;
	wl_resource_set_implementation(wl_resource, &launcher_impl, &desktop->launcher, unbind_desktop);

	wl_list_init(&desktop->launcher.close_listener.link);
	desktop->launcher.close_listener.notify = close_launcher_notify;
}

static void
twdesktop_should_start_launcher(struct weston_keyboard *keyboard,
				const struct timespec *time, uint32_t key,
				void *data)
{
	struct launcher *lch = data;
	taiwins_launcher_send_start(lch->resource,
				    wl_fixed_from_int(200),
				    wl_fixed_from_int(200),
				    wl_fixed_from_int(1));
}


struct twdesktop *
announce_desktop(struct weston_compositor *ec, struct twshell *shell)
{
	//initialize the desktop
	onedesktop.compositor = ec;
	onedesktop.launcher.shell = shell;
	onedesktop.launcher.compositor = ec;
	vector_t *workspaces = &onedesktop.workspaces;
	vector_init(workspaces, sizeof(struct workspace), _free_workspace);
	//then afterwards, you don't spend time allocating workspace anymore
	vector_resize(workspaces, 9);
	for (int i = 0; i < workspaces->len; i++)
		workspace_init((struct workspace *)vector_at(workspaces, i), ec);
	//not sure if this is a good idea, since we are using vector
	onedesktop.actived_workspace[0] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
	onedesktop.actived_workspace[1] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
	switch_workspace(&onedesktop, onedesktop.actived_workspace[0]);
	weston_compositor_add_key_binding(ec, KEY_P, 0, twdesktop_should_start_launcher, &onedesktop.launcher);
	//creating desktop
	onedesktop.api = weston_desktop_create(ec, &desktop_impl, &onedesktop);

	wl_global_create(ec->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onedesktop, bind_desktop);
	return &onedesktop;
}
