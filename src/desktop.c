#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>



#include "taiwins.h"
#include "desktop.h"

struct workspace {
	//workspace does not distinguish the outputs.
	//so when we `switch_workspace, all the output has to update.
	//The layouting algorithm may have to worry about output
	struct weston_layer hidden_layer;
	struct weston_layer tiling_layer;
	struct weston_layer floating_layer;
	//we need a recent_views struct for user to switch among views a link
	//list would be ideal but weston view struct does not have a link for
	//it. The second best choice is a link-list that wraps the view in it,
	//but this requires extensive memory allocation. The next best thing is
	//a stack. Since the recent views and stack share the same logic. We
	//will need a unique stack which can eliminate the duplicated elements.
};

/* workspace related methods */
static void workspace_init(struct workspace *ws, struct weston_compositor *ec);
/* called at compositor destruction */
static void workspace_release(struct workspace *ws);
static void free_workspace(void *ws) { workspace_release((struct workspace *)ws); }
static void workspace_switch(struct workspace *to, struct twdesktop *d);

static inline void workspace_switch_recent(struct twdesktop *d);
/* move all the views in floating layer to hidden */
static void workspace_clear_floating(struct workspace *ws);
/* for a given view, decide whether it is floating view or hidden view */
static void workspace_focus_view(struct workspace *ws, struct weston_view *v);
//what about making view from tiled to float and vice versa?


struct twdesktop {
	struct weston_compositor *compositor;
	//taiwins_launcher
	struct twlauncher *launcher;
	/* managing current status */
	struct workspace *actived_workspace[2];
	//we may need a hidden layer
	vector_t workspaces;

	struct weston_desktop *api;

	struct wl_listener destroy_listener;
};
static struct twdesktop onedesktop;


/**
 * workspace implementation
 */
static void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	weston_layer_init(&wp->tiling_layer, compositor);
	weston_layer_init(&wp->floating_layer, compositor);
	weston_layer_init(&wp->hidden_layer, compositor);
}

static void
workspace_release(struct workspace *ws)
{
	struct weston_view *view, *next;
	struct weston_layer *layers[3]  = {
		&ws->floating_layer,
		&ws->tiling_layer,
		&ws->hidden_layer,
	};
	//get rid of all the surface, maybe
	for (int i = 0; i < 3; i++) {
		if (!wl_list_length(&layers[i]->view_list.link))
			continue;
		wl_list_for_each_safe(view, next,
				      &layers[i]->view_list.link,
				      layer_link.link) {
			struct weston_surface *surface =
				weston_surface_get_main_surface(view->surface);
			weston_surface_destroy(surface);
		}
	}
}

static void
workspace_switch(struct workspace *to, struct twdesktop *d)
{
	struct workspace *ws = d->actived_workspace[0];
	weston_layer_unset_position(&ws->floating_layer);
	weston_layer_unset_position(&ws->tiling_layer);

	d->actived_workspace[1] = ws;
	d->actived_workspace[0] = to;
	weston_layer_set_position(&to->tiling_layer, WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&to->floating_layer , WESTON_LAYER_POSITION_NORMAL+1);
	weston_compositor_schedule_repaint(d->compositor);
}

static inline void workspace_switch_recent(struct twdesktop *d)
{
	workspace_switch(d->actived_workspace[1], d);
}

static void
workspace_clear_floating(struct workspace *ws)
{
	struct weston_view *view, *next;
	if (wl_list_length(&ws->floating_layer.view_list.link))
		wl_list_for_each_safe(view, next,
				      &ws->floating_layer.view_list.link,
				      layer_link.link) {
			weston_layer_entry_remove(&view->layer_link);
			weston_layer_entry_insert(&ws->hidden_layer.view_list,
						  &view->layer_link);
		}
}

static void
workspace_focus_view(struct workspace *ws, struct weston_view *v)
{
	struct weston_layer *l = v->layer_link.layer;
	if (l == &ws->floating_layer) {
		//there is nothing to do, we are already in the top layer.
	} else if (l == &ws->tiling_layer) {
		workspace_clear_floating(ws);
	} else if (l == &ws->hidden_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);
	}
	//it is not possible to be here.
}


/**
 * @brief the libweston-desktop implementation
 */
static void
twdesk_surface_added(struct weston_desktop_surface *surface,
		     void *user_data)
{
	struct weston_view *view, *next;
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link)
		weston_view_destroy(view);

	struct weston_view *wt_view = weston_desktop_surface_create_view(surface);
	wl_list_init(&wt_view->link);

	wt_view->is_mapped = true;
	wt_surface->is_mapped = true;
	weston_desktop_surface_set_activated(surface, true);
	wt_view->output = tw_get_focused_output(wt_surface->compositor);
	wt_surface->output = wt_view->output;
	weston_view_damage_below(wt_view);
	//you need to somehow focus the view, here we are doing it in the hacky
	//way.
	struct weston_seat *active_seat = container_of(onedesktop.compositor->seat_list.next, struct weston_seat, link);
	struct weston_keyboard *keyboard = active_seat->keyboard_state;
	weston_keyboard_set_focus(keyboard, wt_surface);
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
	//again, we don't know which view is committed as well.
	fprintf(stderr, "committed\n");
	struct workspace *wsp = onedesktop.actived_workspace[0];
	struct weston_surface *surface =  weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	//hacky way, we don't know which layer to insert though, need to decide by the layout program
	if (wl_list_empty(&view->layer_link.link))
		weston_layer_entry_insert(&wsp->floating_layer.view_list, &view->layer_link);

	weston_view_set_position(view, 200, 200);
	weston_view_damage_below(view);

	weston_surface_schedule_repaint(surface);

}

//doesn't seems to work!!!
static struct weston_desktop_api desktop_impl =  {
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.struct_size = sizeof(struct weston_desktop_api),
};
/*** libweston-desktop implementation ***/


struct twdesktop *
announce_desktop(struct weston_compositor *ec, struct twlauncher *launcher)
{
	//initialize the desktop
	onedesktop.compositor = ec;
	onedesktop.launcher = launcher;
	vector_t *workspaces = &onedesktop.workspaces;
	vector_init(workspaces, sizeof(struct workspace), free_workspace);
	//then afterwards, you don't spend time allocating workspace anymore
	vector_resize(workspaces, 9);
	for (int i = 0; i < workspaces->len; i++)
		workspace_init((struct workspace *)vector_at(workspaces, i), ec);
	//not sure if this is a good idea, since we are using vector
	onedesktop.actived_workspace[0] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
	onedesktop.actived_workspace[1] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
	workspace_switch(onedesktop.actived_workspace[0], &onedesktop);
	//NOTE this creates the xwayland layer, which is WAYLAND_LAYER_POSITION_NORMAL+1
	onedesktop.api = weston_desktop_create(ec, &desktop_impl, &onedesktop);

//	wl_global_create(ec->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onedesktop, bind_desktop);
	return &onedesktop;
}


//implementation of grab interfaces
struct grab_interface {
	struct weston_pointer_grab pointer_grab;
	struct weston_touch_grab touch_grab;
	struct weston_keyboard_grab keyboard_grab;
	struct weston_view *view;
};

//implement the grab interfaces, basically 3 different interfaces need to be
// implemented.
//
//1) pointer, 2) touch 3) keyboard
// The grab is a state, you need to start and end(basically setting back to
// default grab).  the start grab is usually triggered by libweston
// callbacks(move) and maybe other bindings.
//
// Once the grab is triggered, you have to work on one view. From start to
// end. The grab should stay on the same view, but since there are multiple
// input devices, we cannot assume the we only have one grab at a time(although
// it is true most of the itme). So The idea is allocate a grab_interface when
// starting the grab. The deallocate it when we are done.

static struct weston_pointer_grab_interface twdesktop_grab_impl {

};
