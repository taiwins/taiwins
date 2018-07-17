#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>



#include "taiwins.h"
#include "shell.h"

struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;

	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}


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

static struct desktop onedesktop;

struct launcher *
twshell_acquire_launcher(void)
{
	return &onedesktop.launcher;
}



//the application has to sit on the ui layer. So it has to follow the
//taiwins_shell protocol

//if you call weston_layer_set_position, you will insert the layer into
//compositors layer_list, so we only do that when workspace is active.
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
switch_workspace(struct desktop *d, struct workspace *to)
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
switch_to_recent_workspace(struct desktop *d)
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
switch_layer(struct desktop *d)
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
switch_layer_refresh(struct desktop *d)
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
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	struct workspace *wsp = onedesktop.actived_workspace[0];
	struct weston_view *wt_view = weston_desktop_surface_create_view(surface);
	//yep, I don't think we have a output
	wt_view->is_mapped = true;
	wt_surface->is_mapped = true;
	//I am not sure if I need the output
	weston_view_set_position(wt_view, 0, 0);
	weston_layer_entry_insert(&wsp->shown_float_layout.view_list, &wt_view->layer_link);
//	wl_list_init(struct wl_list *list)
	weston_desktop_surface_set_activated(surface, true);
	struct weston_seat *active_seat = container_of(onedesktop.compositor->seat_list.next, struct weston_seat, link);
	struct weston_keyboard *keyboard = active_seat->keyboard_state;
	weston_keyboard_set_focus(keyboard, wt_surface);
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
//	struct weston_view *v, *next;
//	weston_surface_destroy(wt_surface);
//	weston_desktop_surface_
}

static void
twdesk_surface_committed(struct weston_desktop_surface *desktop_surface,
			 int32_t sx, int32_t sy, void *data)
{
	struct weston_surface *surface =  weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	weston_view_set_position(view, 0, 0);
	weston_view_schedule_repaint(view);

}

//doesn't seems to work!!!
static struct weston_desktop_api desktop_impl =  {
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
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
launcher_committed(struct weston_surface *surface, int32_t sx, int32_t sy)
{

}


static void
set_launcher(struct wl_client *client, struct wl_resource *resource,
	     struct wl_resource *wl_surface, struct wl_resource *buffer)
{

	//I would also need to know the layer, which it is controlled in the shell object
	struct launcher *launcher = twshell_acquire_launcher();
	struct wl_shm_buffer *wl_buffer = wl_shm_buffer_get(buffer);
	launcher->decision_buffer = wl_buffer;
	//now we should show the launcher
	struct weston_surface *wt_surface = weston_surface_from_resource(wl_surface);
	struct weston_view *view, *next;

	if (wt_surface->committed) {
		wl_resource_post_error(wl_surface, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface already have a role");
		return;
	}
	//I seriously don't know what to do
	wt_surface->committed = NULL;
	wt_surface->committed_private = NULL;
	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(wt_surface);
	wt_surface->output = weston_output_from_resource(output);
	view->output = wt_surface->output;
}




static void
close_launcher(struct wl_client *client, struct wl_resource *resource)
{

}


static struct taiwins_launcher_interface launcher_impl = {
	.set_launcher = set_launcher,
};


static void
unbind_desktop(struct wl_resource *r)
{
}

static void
bind_desktop(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client, &taiwins_launcher_interface,
							  TWDESKP_VERSION, id);
	wl_resource_set_implementation(resource, NULL, data, unbind_desktop);
	//TODO we may have to add resource destroy signal here, if the resource
	//is deleted, we should somehow remove it from the

}

/*TODO
 * remove this function later!!!!
 */
void spawn_weston_terminal(struct weston_keyboard *keyboard, const struct timespec *time,
			   uint32_t key, void *data)
{
	fprintf(stderr, "we should open the terminal now!\n");
	char *argv[] = {NULL};
	pid_t pid = fork();
	if (pid == -1) {

	} else if (pid == 0) {
		//okay, we are gonna print everything in the environ
		for (char **env = environ; *env != 0; env++)
		{
			char *thisEnv = *env;
			printf("%s\n", thisEnv);
		}
		//you don't need to setsid since you want to close all the
		//clients as we close, also, child program can close the stdin,
		//stdout, stderr themselves.

		//child process, we should close all the
		//setsid, we should probably setsid, close stdin, stdout
		//what we can do here is changing the envals.
		execvpe("weston-flower", argv, environ);
	} else {
		return;
	}
}



bool
announce_desktop(struct weston_compositor *ec)
{
	//initialize the desktop
	onedesktop.compositor = ec;
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
	//add keybindings to spawn the
//	weston_compositor_add_key_binding(ec, KEY_ENTER, MODIFIER_ALT, spawn_weston_terminal, NULL);
//	weston_compositor_add_button_binding(ec, BTN_LEFT, 0, focus_the_view, NULL);
	onedesktop.api = weston_desktop_create(ec, &desktop_impl, NULL);

	//as we have
	wl_global_create(ec->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onedesktop, bind_desktop);
	return true;
}
