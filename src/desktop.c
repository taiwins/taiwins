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
	struct wl_list layout_tiling_link;
	struct wl_list layout_floating_link;
	//workspace does not distinguish the outputs.
	//so when we `switch_workspace, all the output has to update.
	//The layouting algorithm may have to worry about output
	struct weston_layer hidden_layer;
	struct weston_layer tiling_layer;
	struct weston_layer floating_layer;
	//Recent views::
	//we need a recent_views struct for user to switch among views. FIRST a
	//link list would be ideal but weston view struct does not have a link
	//for it. The SECOND best choice is a link-list that wraps the view in
	//it, but this requires extensive memory allocation. The NEXT best thing
	//is a stack. Since the recent views and stack share the same logic. We
	//will need a unique stack which can eliminate the duplicated elements.

	//the only tiling layer here will create the problem when we want to do
	//the stacking layout, for example. Only show two views.
};

//operation surtenir.
//move view to top(move the view to the top of the list)

//move view up/down only for tile : (find the next view on the same output, then insert after that)
//move view to another workspace(insert the view the correct layer on the correct )
//toggle view float/tiled(insert to the top of corresponding layer)
//cycle through the views(we should unfied)
//resize

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
	//does the desktop should have the shell ui layout? If that is the case,
	//we should get the shell as well.

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
 * grab decleration, with different options.
 */
struct grab_interface {
	struct weston_pointer_grab pointer_grab;
	struct weston_touch_grab touch_grab;
	struct weston_keyboard_grab keyboard_grab;
	struct weston_view *view;
	struct weston_compositor *compositor;
};

static struct grab_interface *
grab_interface_create_for(struct weston_view *view, struct weston_seat *seat);

static void
grab_interface_destroy(struct grab_interface *gi);


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

static void
twdesk_surface_move(struct weston_desktop_surface *desktop_surface,
		    struct weston_seat *seat, uint32_t serial, void *user_data)
{
	struct grab_interface *gi;
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_surface *surface = weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = tw_default_view_from_surface(surface);
//	struct weston_touch *touch = weston_seat_get_touch(seat);
	if (pointer && pointer->focus && pointer->button_count > 0) {
		gi = grab_interface_create_for(view, seat);
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}
}


//doesn't seems to work!!!
static struct weston_desktop_api desktop_impl =  {
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.move = twdesk_surface_move,
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
static struct weston_pointer_grab_interface twdesktop_moving_grab;
static struct weston_pointer_grab_interface twdesktop_zoom_grab;
static struct weston_pointer_grab_interface twdesktop_alpha_grab;

/**
 * constructor, view can be null, but seat cannot. we need compositor
 */
static struct grab_interface *
grab_interface_create_for(struct weston_view *view, struct weston_seat *seat)
{
	assert(seat);
	struct grab_interface *gi = calloc(sizeof(struct grab_interface), 1);
	gi->view = view;
	gi->compositor = seat->compositor;
	//TODO find out the corresponding grab interface
//	gi->pointer_grab.interface = &twdesktop_moving_grab;
	gi->pointer_grab.interface = &twdesktop_alpha_grab;
	gi->pointer_grab.pointer = weston_seat_get_pointer(seat);
	//right now we do not have other grab
	return gi;
}

static void
grab_interface_destroy(struct grab_interface *gi)
{
	free(gi);
}

//implement the grab interfaces, basically 3 different interfaces need to be
// implemented.
//
//1) pointer, 2) touch 3) keyboard
// The grab is a state, you need to start and end(basically setting back to
// default grab).  the start grab is usually triggered by libweston
// callbacks(move) and maybe other bindings.
//
// Once the grab is triggered. the input devices works differently, for example.
// Once super pressed, the pointer gets into moving state, and you have to work
// on one view. From start to end. The grab should stay on the same view, but
// since there are multiple input devices, we cannot assume the we only have one
// grab at a time(although it is true most of the itme). So The idea is allocate
// a grab_interface when starting the grab. The deallocate it when we are done.

static void
constrain_pointer(struct weston_pointer_motion_event *event, struct weston_output *output)
{
	//the actual use of the function is contraining the views so it doesn't
	//overlap the UI elements, but we do not need it here.
}

static void
pointer_motion_delta(struct weston_pointer *p,
				 struct weston_pointer_motion_event *e,
				 double *dx, double *dy)
{

	//so there could be two case, if the motion is abs, the cx, cy is from
	//event->[x,y]. If it is relative, it will be pointer->x + event->dx.
	wl_fixed_t cx, cy;
	if (e->mask & WESTON_POINTER_MOTION_REL) {
		//this is a short cut where you can get back as soon as possible.
		*dx = e->dx;
		*dy = e->dy;
	} else {
		weston_pointer_motion_to_abs(p, e, &cx, &cy);
		*dx = wl_fixed_to_double(cx) - wl_fixed_to_double(p->x);
		*dy = wl_fixed_to_double(cy) - wl_fixed_to_double(p->y);
	}
}


//used in the mopving_grab
static void noop_focus(struct weston_pointer_grab *grab) {}

static void noop_axis(struct weston_pointer_grab *grab, const struct timespec *time,
		      struct weston_pointer_axis_event *event ) {}

//this zoom can also be implement as blending operations
static void
zoom_axis(struct weston_pointer_grab *grab, const struct timespec *time,
	  struct weston_pointer_axis_event *event)
{
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	struct weston_pointer *pointer = gi->pointer_grab.pointer;
	struct weston_seat *seat = gi->pointer_grab.pointer->seat;
	struct weston_output *output;
	double augment;

	wl_list_for_each(output, &gi->compositor->output_list, link) {
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
alpha_axis(struct weston_pointer_grab *grab, const struct timespec *time,
	      struct weston_pointer_axis_event *event)
{
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	struct weston_pointer *pointer = gi->pointer_grab.pointer;
	struct weston_seat *seat = gi->pointer_grab.pointer->seat;
	struct weston_view *view = gi->view;
	float increment = 0.07;
	float sign = (event->has_discrete) ? -1.0 : 1.0;

	if (!view)
		return;
	view->alpha += increment * sign * event->value / 20.0;
	if (view->alpha < 0.0)
		view->alpha = 0.0;
	if (view->alpha > 1.0)
		view->alpha = 1.0;
	weston_view_damage_below(view);
	weston_view_schedule_repaint(view);
}

static void noop_axis_source(struct weston_pointer_grab *grab, uint32_t source) {}

static void noop_frame(struct weston_pointer_grab *grab) {}


static void
move_grab_pointer_motion(struct weston_pointer_grab *grab,
				     const struct timespec *time,
				     struct weston_pointer_motion_event *event)
{
	double dx, dy;
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);

	//but no send_motion_event.
	if (!gi->view)
		return;
//	struct weston_surface *surface = gi->view->surface;
	//TODO constrain the pointer.
	weston_view_set_position(gi->view,
				 gi->view->geometry.x + dx,
				 gi->view->geometry.y + dy);
	weston_view_schedule_repaint(gi->view);

}

static void
noop_grab_pointer_motion(struct weston_pointer_grab *grab, const struct timespec *time,
			 struct weston_pointer_motion_event *event)
{
	weston_pointer_move(grab->pointer, event);
}

//this should be an universal implementation
static void
pointer_grab_cancel(struct weston_pointer_grab *grab)
{
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	weston_pointer_end_grab(grab->pointer);
	grab_interface_destroy(gi);
}


static void
move_grab_button(struct weston_pointer_grab *grab, const struct timespec *time,
			     uint32_t button, uint32_t state)
{
	//free can happen here as well.
	struct weston_pointer *pointer = grab->pointer;
	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED)
		pointer_grab_cancel(grab);
}



static struct weston_pointer_grab_interface twdesktop_moving_grab = {
	.focus = noop_focus,
	.motion = move_grab_pointer_motion,
	.button = move_grab_button,
	.axis = noop_axis,
	.frame = noop_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_axis_source,
};

static struct weston_pointer_grab_interface twdesktop_zoom_grab = {
	.focus = noop_focus,
	.motion = noop_grab_pointer_motion,
	.button = move_grab_button,
	.axis = zoom_axis,
	.frame = noop_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_axis_source,
};

static struct weston_pointer_grab_interface twdesktop_alpha_grab = {
	.focus = noop_focus,
	.motion = noop_grab_pointer_motion,
	.button = move_grab_button,
	.axis = alpha_axis,
	.frame = noop_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_axis_source,
};
