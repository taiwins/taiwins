#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>



#include "taiwins.h"
#include "desktop.h"
#include "layout/layout.h"

struct workspace {
	struct wl_list floating_layout_link;
	struct wl_list tiling_layout_link;
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
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
};
static struct twdesktop onedesktop;



static void workspace_switch(struct workspace *to, struct twdesktop *d);

static inline void workspace_switch_recent(struct twdesktop *d);
/* move all the views in floating layer to hidden */
static void workspace_clear_floating(struct workspace *ws);
/* for a given view, decide whether it is floating view or hidden view */
//you need to use the focus signal to implement the
static bool workspace_focus_view(struct workspace *ws, struct weston_view *v);
//what about making view from tiled to float and vice versa?


/**
 * workspace implementation
 */
static void
workspace_init(struct workspace *wp, struct weston_compositor *compositor)
{
	wl_list_init(&wp->floating_layout_link);
	wl_list_init(&wp->tiling_layout_link);
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

static void free_workspace(void *ws)
{ workspace_release((struct workspace *)ws); }

struct layout *
workspace_get_layout_for_view(const struct workspace *ws, const struct weston_view *v)
{
	struct layout *l;
	if (!v || (v->layer_link.layer != &ws->floating_layer &&
		   v->layer_link.layer != &ws->tiling_layer))
		return NULL;
	wl_list_for_each(l, &ws->floating_layout_link, link) {
		if (l->layer == &ws->floating_layer)
			return l;
	}
	wl_list_for_each(l, &ws->tiling_layout_link, link) {
		if (l->layer == &ws->tiling_layer)
			return l;
	}
	return NULL;
}

static void
arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
			const enum disposer_command command,
			const struct disposer_op *arg)
{
	struct layout *layout = workspace_get_layout_for_view(ws, v);
	if (!layout)
		return;
	int len = wl_list_length(&ws->floating_layer.view_list.link) +
		wl_list_length(&ws->tiling_layer.view_list.link) + 1;
	struct disposer_op ops[len];
	memset(ops, 0, sizeof(ops));
	layout->commander(command, arg, v, layout, ops);
	for (int i = 0; i < len; i++) {
		if (ops[i].end)
			break;
		//TODO, check the validty of the operations
		weston_view_set_position(v, ops[i].pos.x, ops[i].pos.y);
		weston_view_schedule_repaint(v);
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

//BUG!!! couldn't lift the view
static bool
workspace_focus_view(struct workspace *ws, struct weston_view *v)
{
	struct weston_layer *l = (v) ? v->layer_link.layer : NULL;
	if (!l || (l != &ws->floating_layer && l != &ws->tiling_layer))
		return false;
	//move float to hidden is a bit tricky
	if (l == &ws->floating_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);
		//we should somehow ping on it
	} else if (l == &ws->tiling_layer) {
		workspace_clear_floating(ws);
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->tiling_layer.view_list,
					  &v->layer_link);
	} else if (l == &ws->hidden_layer) {
		weston_layer_entry_remove(&v->layer_link);
		weston_layer_entry_insert(&ws->floating_layer.view_list,
					  &v->layer_link);

	}
	weston_view_damage_below(v);
	weston_view_schedule_repaint(v);
	return true;
	//it is not possible to be here.
}


static void
workspace_add_output(struct workspace *wp, struct weston_output *output)
{
	if (wl_list_empty(&wp->floating_layout_link)) {
		struct layout *fl = floatlayout_create(&wp->floating_layer, output);
		wl_list_insert(&wp->floating_layout_link, &fl->link);
	}
	//TODO create the tiling_layout as well.
}

static void
workspace_remove_output(struct workspace *w, struct weston_output *output)
{
	struct layout *l, *next;
	wl_list_for_each_safe(l, next, &w->floating_layout_link, link) {
		wl_list_remove(&l->link);
		floatlayout_destroy(l);
		//some how you need to move all the layer here
	}
	wl_list_for_each_safe(l, next, &w->tiling_layout_link, link) {
	}
}



/**
 * grab decleration, with different options.
 */
struct grab_interface {
	struct weston_pointer_grab pointer_grab;
	struct weston_touch_grab touch_grab;
	struct weston_keyboard_grab keyboard_grab;
	/* need this struct to access the workspace */
	struct twdesktop *desktop;
	struct weston_view *view;
	struct weston_compositor *compositor;
};

static struct grab_interface *
grab_interface_create_for(struct weston_view *view, struct weston_seat *seat, struct twdesktop *desktop);

static void
grab_interface_destroy(struct grab_interface *gi);


/*********************************************************************/
/****************      weston_desktop impl            ****************/
/*********************************************************************/
static inline bool
is_view_on_twdesktop(const struct weston_view *v, const struct twdesktop *desk)
{
	struct workspace *ws = desk->actived_workspace[0];
	struct weston_layer *layer = v->layer_link.layer;
	return (layer == &ws->floating_layer || layer == &ws->tiling_layer);
}


static void
twdesk_surface_added(struct weston_desktop_surface *surface,
		     void *user_data)
{
	struct twdesktop *desktop = user_data;
	//remove old view (if any) and create one
	struct weston_view *view, *next;
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_desktop_surface_create_view(surface);
	wl_list_init(&view->link);
	view->is_mapped = true;
	wt_surface->is_mapped = true;
	weston_desktop_surface_set_activated(surface, true);
	view->output = tw_get_focused_output(wt_surface->compositor);
	wt_surface->output = view->output;
	//focus on it
	struct weston_seat *active_seat = container_of(desktop->compositor->seat_list.next,
						       struct weston_seat, link);
	struct weston_keyboard *keyboard = active_seat->keyboard_state;
	struct workspace *wsp = desktop->actived_workspace[0];

	weston_keyboard_set_focus(keyboard, wt_surface);
	if (wl_list_empty(&view->layer_link.link))
		weston_layer_entry_insert(&wsp->floating_layer.view_list, &view->layer_link);
	weston_view_set_position(view, 200, 200);
}

static void
twdesk_surface_removed(struct weston_desktop_surface *surface,
		       void *user_data)
{
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	//remove or destroy?
	weston_surface_unmap(wt_surface);
}


static void
twdesk_surface_committed(struct weston_desktop_surface *desktop_surface,
			 int32_t sx, int32_t sy, void *data)
{
	//again, we don't know which view is committed as well.
	fprintf(stderr, "committed\n");

	struct weston_surface *surface =  weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	//hacky way, we don't know which layer to insert though, need to decide by the layout program

	weston_view_damage_below(view);
	weston_view_schedule_repaint(view);
}

static void
twdesk_surface_move(struct weston_desktop_surface *desktop_surface,
		    struct weston_seat *seat, uint32_t serial, void *user_data)
{
	struct grab_interface *gi;
	struct twdesktop *desktop = user_data;
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_surface *surface = weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = tw_default_view_from_surface(surface);
//	struct weston_touch *touch = weston_seat_get_touch(seat);
	if (pointer && pointer->focus && pointer->button_count > 0) {
		gi = grab_interface_create_for(view, seat, desktop);
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

static void
twdesktop_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	for (int i = 0; i < onedesktop.workspaces.len; i++) {
		struct workspace *w = (struct workspace *)
			vector_at(&onedesktop.workspaces, i);
		workspace_add_output(w, output);
	}
}

static void
twdesktop_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	for (int i = 0; i < onedesktop.workspaces.len; i++) {
		struct workspace *w = (struct workspace *)
			vector_at(&onedesktop.workspaces, i);
		//you somehow need to move the views to other output.
		workspace_remove_output(w, output);
	}
}

struct twdesktop *
announce_desktop(struct weston_compositor *ec, struct twlauncher *launcher)
{
	//initialize the desktop
	onedesktop.compositor = ec;
	onedesktop.launcher = launcher;
	{
		vector_t *workspaces = &onedesktop.workspaces;
		vector_init(workspaces, sizeof(struct workspace), free_workspace);
		vector_resize(workspaces, 9);
		for (int i = 0; i < workspaces->len; i++)
			workspace_init((struct workspace *)vector_at(workspaces, i), ec);
		onedesktop.actived_workspace[0] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
		onedesktop.actived_workspace[1] = (struct workspace *)vector_at(&onedesktop.workspaces, 0);
		workspace_switch(onedesktop.actived_workspace[0], &onedesktop);
	}
	/// create the desktop api
	//NOTE this creates the xwayland layer, which is WAYLAND_LAYER_POSITION_NORMAL+1
	onedesktop.api = weston_desktop_create(ec, &desktop_impl, &onedesktop);
	{
		struct weston_output *output;

		wl_list_init(&onedesktop.output_create_listener.link);
		wl_list_init(&onedesktop.output_destroy_listener.link);

		onedesktop.output_create_listener.notify = twdesktop_output_created;
		onedesktop.output_destroy_listener.notify = twdesktop_output_destroyed;

		//add existing output
		wl_signal_add(&ec->output_created_signal,
			      &onedesktop.output_create_listener);
		wl_signal_add(&ec->output_destroyed_signal,
			      &onedesktop.output_destroy_listener);

		wl_list_for_each(output, &ec->output_list, link)
			twdesktop_output_created(&onedesktop.output_create_listener,
						 output);
	}

//	wl_global_create(ec->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onedesktop, bind_desktop);
	return &onedesktop;
}


static struct weston_pointer_grab_interface twdesktop_moving_grab;

/**
 * constructor, view can be null, but seat cannot. we need compositor
 */
static struct grab_interface *
grab_interface_create_for(struct weston_view *view, struct weston_seat *seat, struct twdesktop *desktop)
{
	assert(seat);
	struct grab_interface *gi = calloc(sizeof(struct grab_interface), 1);
	gi->view = view;
	gi->compositor = seat->compositor;
	gi->desktop = desktop;
	//TODO find out the corresponding grab interface
	gi->pointer_grab.interface = &twdesktop_moving_grab;
	gi->pointer_grab.pointer = weston_seat_get_pointer(seat);
	//right now we do not have other grab
	return gi;
}

static void
grab_interface_destroy(struct grab_interface *gi)
{
	free(gi);
}

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


static void
zoom_axis(struct weston_pointer *pointer, const struct timespec *time,
	   struct weston_pointer_axis_event *event, struct weston_compositor *ec)
{
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
alpha_axis(struct weston_pointer *pointer, const struct timespec *time,
	   struct weston_pointer_axis_event *event, struct weston_view *view)
{
	float increment = 0.07;
	float sign = (event->has_discrete) ? -1.0 : 1.0;

	view->alpha += increment * sign * event->value / 20.0;
	if (view->alpha < 0.0)
		view->alpha = 0.0;
	if (view->alpha > 1.0)
		view->alpha = 1.0;
	weston_view_damage_below(view);
	weston_view_schedule_repaint(view);
}

/*********************************************************************/
/****************             GRABS                   ****************/
/*********************************************************************/
static void noop_grab_focus(struct weston_pointer_grab *grab) {}

static void noop_grab_axis(struct weston_pointer_grab *grab, const struct timespec *time,
		      struct weston_pointer_axis_event *event ) {}

static void noop_grab_axis_source(struct weston_pointer_grab *grab, uint32_t source) {}

static void noop_grab_frame(struct weston_pointer_grab *grab) {}


static void
move_grab_pointer_motion(struct weston_pointer_grab *grab,
			 const struct timespec *time,
			 struct weston_pointer_motion_event *event)
{
	double dx, dy;
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	struct twdesktop *d = gi->desktop;
	struct weston_layer *layer = gi->view->layer_link.layer;
	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;
	//TODO constrain the pointer.
	if (layer == &ws->floating_layer) {
		weston_view_set_position(gi->view,
					 gi->view->geometry.x + dx,
					 gi->view->geometry.y + dy);
		weston_view_schedule_repaint(gi->view);
	} else {

		struct disposer_op arg = {
			.v = gi->view,
			.pos = {
				gi->view->geometry.x + dx,
				gi->view->geometry.y + dy,
			},
			.visible = true,
		};
		//this can be problematic
		arrange_view_for_workspace(ws, gi->view, DPSR_deplace, &arg);
	}

}

static void
noop_grab_pointer_motion(struct weston_pointer_grab *grab, const struct timespec *time,
			 struct weston_pointer_motion_event *event)
{
	weston_pointer_move(grab->pointer, event);
}


static void
pointer_grab_cancel(struct weston_pointer_grab *grab)
{
	//an universal implemention, destroy the grab all the time
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
	.focus = noop_grab_focus,
	.motion = move_grab_pointer_motion,
	.button = move_grab_button,
	.axis = noop_grab_axis,
	.frame = noop_grab_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_grab_axis_source,
};


static void
twdesktop_zoom_axis(struct weston_pointer *pointer,
		    const struct timespec *time,
		    struct weston_pointer_axis_event *event,
		    void *data)
{
	struct twdesktop *desktop = data;
	zoom_axis(pointer, time, event, desktop->compositor);
}

static void
twdesktop_alpha_axis(struct weston_pointer *pointer,
		     const struct timespec *time,
		     struct weston_pointer_axis_event *event,
		     void *data)
{
	struct twdesktop *desktop = data;
	//find a view.
	if (!pointer->focus ||
	    !is_view_on_twdesktop(pointer->focus, desktop))
		return;
	alpha_axis(pointer, time, event, pointer->focus);
}


/* I have to implement this like a grab */
static void
twdesktop_move_btn(struct weston_pointer *pointer, const struct timespec *time,
		   uint32_t button, void *data)
{
	struct grab_interface *gi = NULL;
	struct weston_seat *seat = pointer->seat;
	struct weston_view *view = pointer->focus;
	struct twdesktop *desktop = data;
	if (pointer->button_count > 0 && view && is_view_on_twdesktop(view, desktop)) {
		gi = grab_interface_create_for(view, seat, desktop);
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}

}

/*this is useless, we do not know where to move the view*/
static void
twdesktop_deplace_key(struct weston_keyboard *keyboard,
		      const struct timespec *time,
		      uint32_t key,
		      void *data)
{
	//okay, this works, but we will need other things
	struct twdesktop *desktop = data;
	enum disposer_command command;
	struct disposer_op arg;
	struct weston_view *view = keyboard->seat->pointer_state->focus;
	struct workspace *ws = desktop->actived_workspace[0];
	if (!view)
		return;
	arg.visible = true;
	arg.v = view;
	arg.end = false;
	switch (key) {
	case KEY_UP:
		command = DPSR_up;
		break;
	case KEY_DOWN:
		command = DPSR_down;
		break;
	case KEY_LEFT:
		command = DPSR_left;
		break;
	case KEY_RIGHT:
		command = DPSR_right;
		break;
	default:
		return;
	}
	arrange_view_for_workspace(ws, view, command, &arg);
}

static void
twdesktop_click_activate_view(struct weston_pointer *pointer,
			      const struct timespec *time,
			      uint32_t button, void *data)
{
	struct twdesktop *desktop = data;
	struct workspace *ws = desktop->actived_workspace[0];
	if (pointer->grab != &pointer->default_grab)
		return;
	if (!pointer->focus || !pointer->button_count)
		return;
	if (workspace_focus_view(ws, pointer->focus)) {
		weston_view_activate(pointer->focus, pointer->seat,
				     WESTON_ACTIVATE_FLAG_CLICKED);
		struct weston_desktop_surface *s =
			weston_surface_get_desktop_surface(pointer->focus->surface);
		weston_desktop_client_ping(
			weston_desktop_surface_get_client(s));
	}
}

static void
twdesktop_touch_activate_view(struct weston_touch *touch,
			      const struct timespec *time,
			      void *data)
{
	struct twdesktop *desktop = data;
	if (touch->grab != &touch->default_grab || !touch->focus)
		return;
	struct workspace *ws = desktop->actived_workspace[0];
	if (workspace_focus_view(ws, touch->focus)) {
		weston_view_activate(touch->focus, touch->seat,
				     WESTON_ACTIVATE_FLAG_CONFIGURE);
		struct weston_desktop_surface *s =
			weston_surface_get_desktop_surface(touch->focus->surface);
		weston_desktop_client_ping(
			weston_desktop_surface_get_client(s));
	}
}

weston_axis_binding_handler_t twdesktop_zoom_binding = &twdesktop_zoom_axis;
weston_axis_binding_handler_t twdesktop_alpha_binding = &twdesktop_alpha_axis;
weston_button_binding_handler_t twdesktop_move_binding = &twdesktop_move_btn;
weston_button_binding_handler_t twdesktop_click_focus_binding = &twdesktop_click_activate_view;
weston_touch_binding_handler_t  twdesktop_touch_focus_binding = &twdesktop_touch_activate_view;
//weston_key_binding_handler_t twdesktop_deplace_binding = &twdesktop_deplace_key;
