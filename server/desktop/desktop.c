#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <wayland-server.h>
#include <helpers.h>
#include <sequential.h>


#include "../taiwins.h"
#include "desktop.h"
#include "layout.h"
#include "workspace.h"

#define MAX_WORKSPACE 8

struct twdesktop {
	//does the desktop should have the shell ui layout? If that is the case,
	//we should get the shell as well.
	struct weston_compositor *compositor;
	//why do I need the launcher here?
	struct twlauncher *launcher;
	/* managing current status */
	struct workspace *actived_workspace[2];
	struct workspace workspaces[9];

	struct weston_desktop *api;

	struct wl_listener destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
};
static struct twdesktop onedesktop;

static inline off_t
get_workspace_index(struct workspace *ws, struct twdesktop *d)
{
	return ws - d->workspaces;
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
	return is_view_on_workspace(v, desk->actived_workspace[0]);
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
	workspace_add_view(wsp, view);
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
	//we need to add resize, fullscreen requested,
	//maximized requested, minimize requested
	.struct_size = sizeof(struct weston_desktop_api),
};
/*** libweston-desktop implementation ***/

static void
twdesktop_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		workspace_add_output(&onedesktop.workspaces[i], output);
	}
}

static void
twdesktop_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;

	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		struct workspace *w = &onedesktop.workspaces[i];
		//you somehow need to move the views to other output
		workspace_remove_output(w, output);
	}
}

struct twdesktop *
announce_desktop(struct weston_compositor *ec, struct twlauncher *launcher)
{
	//initialize the desktop
	onedesktop.compositor = ec;
	onedesktop.launcher = launcher;
	struct workspace *wss = &onedesktop.workspaces;
	{
		for (int i = 0; i < MAX_WORKSPACE+1; i++)
			workspace_init(&wss[i], ec);
		onedesktop.actived_workspace[0] = &wss[0];
		onedesktop.actived_workspace[1] = &wss[1];
		workspace_switch(onedesktop.actived_workspace[0], onedesktop.actived_workspace[0], NULL);
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


void
end_twdesktop(struct twdesktop *desktop)
{
	//remove listeners
	wl_list_remove(&desktop->output_create_listener.link);
	wl_list_remove(&desktop->output_destroy_listener.link);
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_release(&desktop->workspaces[i]);
	weston_desktop_destroy(desktop->api);
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

	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;
	//TODO constrain the pointer.
	if (!workspace_move_view(ws, gi->view,
					  &(struct weston_position) {
						  gi->view->geometry.x + dx,
							  gi->view->geometry.y + dy}))
	{
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

static void
twdesktop_workspace_switch(struct weston_keyboard *keyboard,
			   const struct timespec *time, uint32_t key,
			   void *data)
{
	struct twdesktop *desktop = data;
	struct workspace *ws = desktop->actived_workspace[0];
	off_t ws_idx = get_workspace_index(ws, desktop);
	desktop->actived_workspace[1] = ws;
	if (key >= KEY_1 && key <= KEY_9) {
		ws_idx = key - KEY_1;
	} else if (key == KEY_LEFT)
		ws_idx = max(0, ws_idx-1);
	else if (key == KEY_RIGHT) {
		ws_idx = min(MAX_WORKSPACE, ws_idx+1);
	}
	desktop->actived_workspace[0] = &desktop->workspaces[ws_idx];
	workspace_switch(&desktop->workspaces[ws_idx], ws, keyboard);
}


static void
twdesktop_workspace_switch_recent(struct weston_keyboard *keyboard,
				  const struct timespec *time, uint32_t key,
				  void *data)
{
	struct twdesktop *desktop = data;
	swap(desktop->actived_workspace[0], desktop->actived_workspace[1]);
	workspace_switch(desktop->actived_workspace[0], desktop->actived_workspace[1], keyboard);
}

weston_key_binding_handler_t twdesktop_workspace_switch_binding = &twdesktop_workspace_switch;
weston_key_binding_handler_t twdesktop_workspace_switch_recent_binding =
	&twdesktop_workspace_switch_recent;
//why am I doing this?
weston_axis_binding_handler_t twdesktop_zoom_binding = &twdesktop_zoom_axis;
weston_axis_binding_handler_t twdesktop_alpha_binding = &twdesktop_alpha_axis;
weston_button_binding_handler_t twdesktop_move_binding = &twdesktop_move_btn;
weston_button_binding_handler_t twdesktop_click_focus_binding = &twdesktop_click_activate_view;
weston_touch_binding_handler_t  twdesktop_touch_focus_binding = &twdesktop_touch_activate_view;
//weston_key_binding_handler_t twdesktop_deplace_binding = &twdesktop_deplace_key;
