#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <wayland-server.h>
#include <helpers.h>
#include <sequential.h>
#include <wayland-taiwins-desktop-server-protocol.h>

#include "../taiwins.h"
#include "../desktop.h"
#include "../config.h"
#include "layout.h"
#include "workspace.h"

#define MAX_WORKSPACE 8

//enums used in the options
enum desktop_switch_workspace_option {
	SWITCH_WS_LEFT, SWITCH_WS_RIGHT
};


struct desktop {
	//does the desktop should have the shell ui layout? If that is the case,
	//we should get the shell as well.
	struct weston_compositor *compositor;
	//why do I need the launcher here?
	/* managing current status */
	struct workspace *actived_workspace[2];
	struct workspace workspaces[9];

	struct weston_desktop *api;

	struct wl_listener destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_resize_listener;
	struct wl_listener output_destroy_listener;
};
static struct desktop DESKTOP;

static inline off_t
get_workspace_index(struct workspace *ws, struct desktop *d)
{
	return ws - d->workspaces;
}

static inline struct workspace*
get_workspace_for_view(struct weston_view *v, struct desktop *d)
{
	struct workspace *wp = NULL;
	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		wp = &d->workspaces[i];
		if (is_view_on_workspace(v, wp))
			break;
	}
	return wp;
}


/**
 * grab decleration, with different options.
 */
struct grab_interface {
	struct weston_pointer_grab pointer_grab;
	struct weston_touch_grab touch_grab;
	struct weston_keyboard_grab keyboard_grab;
	/* need this struct to access the workspace */
	struct desktop *desktop;
	struct weston_view *view;
	struct weston_compositor *compositor;
};

static struct grab_interface *
grab_interface_create_for_pointer(struct weston_view *view, struct weston_seat *seat, struct desktop *desktop,
	struct weston_pointer_grab_interface *i);

static void
grab_interface_destroy(struct grab_interface *gi);


static struct weston_pointer_grab_interface desktop_moving_grab;
static struct weston_pointer_grab_interface desktop_resizing_grab;


/*********************************************************************/
/****************      weston_desktop impl            ****************/
/*********************************************************************/

/*
 * here we are facing this problem, desktop_view has an additional geometry. The
 * content within that geometry is visible. Malheureusement, this geometry is
 * only available at commit. AND IT CAN CHANGE
 *
 * what we can do is that we map this view into an offscreen space, then remap
 * it back. When on the commit, we can have them back
 */
static inline bool
is_view_on_desktop(const struct weston_view *v, const struct desktop *desk)
{
	return is_view_on_workspace(v, desk->actived_workspace[0]);
}

static void
twdesk_surface_added(struct weston_desktop_surface *surface,
		     void *user_data)
{
	struct desktop *desktop = user_data;
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
	struct workspace *wsp = desktop->actived_workspace[0];
	struct recent_view *rv = recent_view_create(view);
	weston_desktop_surface_set_user_data(surface, rv);

	workspace_add_view(wsp, view);
	tw_focus_surface(wt_surface);
}

static void
twdesk_surface_removed(struct weston_desktop_surface *surface,
		       void *user_data)
{
	struct desktop *desktop = user_data;
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	struct weston_view *view, *next;
	//although this should never happen, but if desktop destroyed is not on
	//current view, we have to deal with that as well.
	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link) {
		struct workspace *wp = get_workspace_for_view(view, desktop);
		workspace_remove_view(wp, view);
		weston_view_unmap(view);
		if (!weston_surface_is_mapped(wt_surface))
			weston_view_destroy(view);
	}
	//you do not need to destroy the surface, it gets destroyed
	//when client destroys himself
	weston_surface_set_label_func(wt_surface, NULL);
	weston_surface_unmap(wt_surface);
	//destroy the recent view
	struct recent_view *rv =
		weston_desktop_surface_get_user_data(surface);
	recent_view_destroy(rv);
	//focus a surface
	struct workspace *ws = desktop->actived_workspace[0];
	if (wl_list_length(&ws->recent_views)) {
		rv = container_of(ws->recent_views.next,
				  struct recent_view, link);
		wt_surface = rv->view->surface;
		workspace_focus_view(ws, rv->view);
		tw_focus_surface(wt_surface);
	}
}

static void
twdesk_surface_committed(struct weston_desktop_surface *desktop_surface,
			 int32_t sx, int32_t sy, void *data)
{
	struct weston_surface *surface =  weston_desktop_surface_get_surface(desktop_surface);
	struct recent_view *rv = weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_view *view = container_of(surface->views.next, struct weston_view, surface_link);
	struct weston_geometry geo = weston_desktop_surface_get_geometry(desktop_surface);
	//check the current surface geometry
	if (geo.x != rv->old_geometry.x || geo.y != rv->old_geometry.y) {
		float x, y;
		recent_view_get_origin_coord(rv, &x, &y);
		weston_view_set_position(view, x - geo.x, y - geo.y);
		weston_view_geometry_dirty(view);
		rv->old_geometry = geo;
	}
	weston_view_damage_below(view);
	weston_view_schedule_repaint(view);
}

static void
twdesk_surface_move(struct weston_desktop_surface *desktop_surface,
		    struct weston_seat *seat, uint32_t serial, void *user_data)
{
	struct grab_interface *gi;
	struct desktop *desktop = user_data;
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_surface *surface = weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = tw_default_view_from_surface(surface);
//	struct weston_touch *touch = weston_seat_get_touch(seat);
	if (pointer && pointer->focus && pointer->button_count > 0) {
		gi = grab_interface_create_for_pointer(view, seat, desktop,
						       &desktop_moving_grab);
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}
}


static void
twdesk_surface_resize(struct weston_desktop_surface *desktop_surface,
		      struct weston_seat *seat, uint32_t serial,
		      enum weston_desktop_surface_edge edges, void *user_data)
{
	struct grab_interface *gi;
	struct desktop *desktop = user_data;
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_surface *surface = weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = tw_default_view_from_surface(surface);
	if (pointer && pointer->focus && pointer->button_count > 0) {
		gi = grab_interface_create_for_pointer(view, seat, desktop,
						       &desktop_resizing_grab);
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}
}


static struct weston_desktop_api desktop_impl =  {
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.move = twdesk_surface_move,
	.resize = twdesk_surface_resize,
	//we need to add resize, fullscreen requested,
	//maximized requested, minimize requested
	.struct_size = sizeof(struct weston_desktop_api),
};
/*** libweston-desktop implementation ***/


static void
desktop_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct taiwins_output taiwins_output = {
		.output = output,
		.desktop_area = {
			output->x, output->y + 32,
			output->width, output->height - 32,
		},
		.inner_gap = 10,
		.outer_gap = 10,
	};
	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		workspace_add_output(&DESKTOP.workspaces[i], &taiwins_output);
	}
}

static void
desktop_output_resized(struct wl_listener *listenr, void *data)
{
	struct weston_output *output = data;
	struct taiwins_output taiwins_output = {
		.output = output,
		.desktop_area = {
			output->x, output->y + 32,
			output->width, output->height - 32,
		},
		.inner_gap = 10,
		.outer_gap = 10,
	};
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_resize_output(&DESKTOP.workspaces[i], &taiwins_output);
}


static void
desktop_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;

	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		struct workspace *w = &DESKTOP.workspaces[i];
		//you somehow need to move the views to other output
		workspace_remove_output(w, output);
	}
}

static void
desktop_add_bindings(void *data, struct tw_bindings *bindings, struct taiwins_config *c);

struct desktop *
announce_desktop(struct weston_compositor *ec, struct taiwins_config *config)
{
	//initialize the desktop
	DESKTOP.compositor = ec;
	struct workspace *wss = DESKTOP.workspaces;
	{
		for (int i = 0; i < MAX_WORKSPACE+1; i++)
			workspace_init(&wss[i], ec);
		DESKTOP.actived_workspace[0] = &wss[0];
		DESKTOP.actived_workspace[1] = &wss[1];
		workspace_switch(DESKTOP.actived_workspace[0], DESKTOP.actived_workspace[0], NULL);
	}
	/// create the desktop api
	//NOTE this creates the xwayland layer, which is WAYLAND_LAYER_POSITION_NORMAL+1
	DESKTOP.api = weston_desktop_create(ec, &desktop_impl, &DESKTOP);
	{
		struct weston_output *output;

		wl_list_init(&DESKTOP.output_create_listener.link);
		wl_list_init(&DESKTOP.output_destroy_listener.link);
		wl_list_init(&DESKTOP.output_resize_listener.link);

		DESKTOP.output_create_listener.notify = desktop_output_created;
		DESKTOP.output_destroy_listener.notify = desktop_output_destroyed;
		DESKTOP.output_resize_listener.notify = desktop_output_resized;

		//add existing output
		wl_signal_add(&ec->output_created_signal,
			      &DESKTOP.output_create_listener);
		wl_signal_add(&ec->output_resized_signal,
			      &DESKTOP.output_resize_listener);
		wl_signal_add(&ec->output_destroyed_signal,
			      &DESKTOP.output_destroy_listener);

		wl_list_for_each(output, &ec->output_list, link)
			desktop_output_created(&DESKTOP.output_create_listener,
						 output);
	}
	//last step, add keybindings
	taiwins_config_register_bindings_funcs(config, taiwins_config_get_bindings(config), desktop_add_bindings, &DESKTOP);
	return &DESKTOP;
}


void
end_desktop(struct desktop *desktop)
{
	//remove listeners
	wl_list_remove(&desktop->output_create_listener.link);
	wl_list_remove(&desktop->output_destroy_listener.link);
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_release(&desktop->workspaces[i]);
	weston_desktop_destroy(desktop->api);
}


/**
 * constructor, view can be null, but seat cannot. we need compositor
 */
static struct grab_interface *
grab_interface_create_for_pointer(struct weston_view *view, struct weston_seat *seat, struct desktop *desktop,
	struct weston_pointer_grab_interface *g)
{
	assert(seat);
	struct grab_interface *gi = calloc(sizeof(struct grab_interface), 1);
	gi->view = view;
	gi->compositor = seat->compositor;
	gi->desktop = desktop;
	//TODO find out the corresponding grab interface
	gi->pointer_grab.interface = g;
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
alpha_axis(struct weston_pointer *pointer, struct weston_pointer_axis_event *event,
	   struct weston_view *view)
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
	struct desktop *d = gi->desktop;

	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;
	//so this function will have no effect on tiling views

	//TODO constrain the pointer.
	if (!workspace_move_view(ws, gi->view,
					  &(struct weston_position) {
						  gi->view->geometry.x + dx,
							  gi->view->geometry.y + dy}))
	{
		struct layout_op arg = {
			.v = gi->view,
			.pos = {
				gi->view->geometry.x + dx,
				gi->view->geometry.y + dy,
			},
		};
		//this can be problematic
		arrange_view_for_workspace(ws, gi->view, DPSR_deplace, &arg);
	}
}


static void
resize_grab_pointer_motion(struct weston_pointer_grab *grab,
			   const struct timespec *time,
			   struct weston_pointer_motion_event *event)
{
	double dx, dy;
	struct grab_interface *gi = container_of(grab, struct grab_interface, pointer_grab);
	struct desktop *d = gi->desktop;

	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;

	//then we have to encode the resizing event into
	//now we deterine the motion
	float x = wl_fixed_to_int(grab->pointer->x);
	float y = wl_fixed_to_int(grab->pointer->y);

	struct layout_op arg = {
		.v = gi->view,
		.dx = dx,
		.dy = dy,
		.sx = x,
		.sy = y,
	};
	arrange_view_for_workspace(ws, gi->view, DPSR_resize, &arg);
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


static struct weston_pointer_grab_interface desktop_moving_grab = {
	.focus = noop_grab_focus,
	.motion = move_grab_pointer_motion,
	.button = move_grab_button,
	.axis = noop_grab_axis,
	.frame = noop_grab_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_grab_axis_source,
};


static struct weston_pointer_grab_interface desktop_resizing_grab = {
	.focus = noop_grab_focus,
	.motion = resize_grab_pointer_motion,
	.button = move_grab_button,
	.axis = noop_grab_axis,
	.frame = noop_grab_frame,
	.cancel = pointer_grab_cancel,
	.axis_source = noop_grab_axis_source,
};


static void
desktop_alpha_axis(struct weston_pointer *pointer,
		   uint32_t option,
		   struct weston_pointer_axis_event *event,
		   void *data)
{
	struct desktop *desktop = data;
	//find a view.
	if (!pointer->focus ||
	    !is_view_on_desktop(pointer->focus, desktop))
		return;
	alpha_axis(pointer, event, pointer->focus);
}

static void
desktop_click_move(struct weston_pointer *pointer,
		 const struct timespec *time, uint32_t button, void *data)
{
	struct grab_interface *gi = NULL;
	struct weston_seat *seat = pointer->seat;
	struct weston_view *view = pointer->focus;
	struct desktop *desktop = data;
	if (pointer->button_count > 0 && view && is_view_on_desktop(view, desktop)) {
		gi = grab_interface_create_for_pointer(view, seat, desktop,
						       &desktop_moving_grab);
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}

}

static void
desktop_click_activate_view(struct weston_pointer *pointer,
			    const struct timespec *time,
			    uint32_t button, void *data)
{
	struct desktop *desktop = data;
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
	//TODO have shell_focus_view as well!!
}

static void
desktop_touch_activate_view(struct weston_touch *touch,
			    const struct timespec *time,
			    void *data)
{
	struct desktop *desktop = data;
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
desktop_workspace_switch(struct weston_keyboard *keyboard,
			 const struct timespec *time,
			 uint32_t key, uint32_t option,
			 void *data)
{
	struct desktop *desktop = data;
	struct workspace *ws = desktop->actived_workspace[0];
	off_t ws_idx = get_workspace_index(ws, desktop);
	desktop->actived_workspace[1] = ws;
	if (option == SWITCH_WS_LEFT)
		ws_idx = MAX(0, ws_idx-1);
	else
		ws_idx = MIN(MAX_WORKSPACE, ws_idx+1);
	desktop->actived_workspace[0] = &desktop->workspaces[ws_idx];
	workspace_switch(&desktop->workspaces[ws_idx], ws, keyboard);
}


static void
desktop_workspace_switch_recent(struct weston_keyboard *keyboard,
				const struct timespec *time,
				uint32_t key, uint32_t option,
				void *data)
{
	struct desktop *desktop = data;
	SWAP(desktop->actived_workspace[0], desktop->actived_workspace[1]);
	workspace_switch(desktop->actived_workspace[0], desktop->actived_workspace[1], keyboard);
}

//not sure if we want to make it here
enum desktop_view_resize_option {
	RESIZE_LEFT, RESIZE_RIGHT,
	RESIZE_UP, RESIZE_DOWN,
};

static void
desktop_view_resize(struct weston_keyboard *keyboard,
		    const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	//as a keybinding, we only operate on the lower button of the view
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	struct layout_op arg = {
		.v = view,
	};
	switch (option) {
	case RESIZE_LEFT:
		arg.dx = -10;
		break;
	case RESIZE_RIGHT:
		arg.dx = 10;
		break;
	case RESIZE_UP:
		arg.dy = -10;
		break;
	case RESIZE_DOWN:
		arg.dy = 10;
	default:
		arg.dx = 10;
	}
	weston_view_to_global_float(view,
				    view->surface->width-1,
				    view->surface->height-1,
				    &arg.sx, &arg.sy);
	arrange_view_for_workspace(ws, view, DPSR_resize, &arg);
}

static void
desktop_toggle_vertical(struct weston_keyboard *keyboard,
			const struct timespec *time, uint32_t key,
			uint32_t option, void *data)
{
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	struct layout_op arg = {
		.v = view,
	};
	arrange_view_for_workspace(ws, view, DPSR_toggle, &arg);
}

static void
desktop_toggle_floating(struct weston_keyboard *keyboard,
			const struct timespec *time, uint32_t key,
			uint32_t option, void *data)
{
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	if (ws)
		workspace_switch_layout(ws, view);
}

static void
desktop_split_view(struct weston_keyboard *keyboard,
		   const struct timespec *time, uint32_t key,
		   uint32_t option, void *data)
{
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	struct layout_op arg = {
		.v = view,
		.vertical_split = option == 0,
	};

	if (ws)
		arrange_view_for_workspace(ws, view, DPSR_split, &arg);

}

static void
desktop_merge_view(struct weston_keyboard *keyboard,
		   const struct timespec *time, uint32_t key,
		   uint32_t option, void *data)
{
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	struct layout_op arg = {
		.v = view,
	};
	if (ws)
		arrange_view_for_workspace(ws, view, DPSR_merge, &arg);
}

static void
desktop_recent_view(struct weston_keyboard *keyboard,
		    const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	struct recent_view *rv = get_recent_view(view);
	wl_list_remove(&rv->link);
	wl_list_insert(ws->recent_views.prev, &rv->link);
	struct recent_view *nrv =
		container_of(ws->recent_views.next,
			     struct recent_view, link);
	workspace_focus_view(ws, nrv->view);
	tw_focus_surface(nrv->view->surface);
}

static void
desktop_add_bindings(void *data, struct tw_bindings *bindings, struct taiwins_config *c)
{
	struct desktop *d = data;

	//////////////////////////////////////////////////////////
	//move press
	struct tw_btn_press move_press =
		taiwins_config_get_builtin_binding(c, TW_MOVE_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &move_press, desktop_click_move, d);

	//////////////////////////////////////////////////////////
	//focus press
	struct tw_btn_press focus_press =
		taiwins_config_get_builtin_binding(c, TW_FOCUS_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &focus_press,
			    desktop_click_activate_view, d);
	tw_bindings_add_touch(bindings, 0, desktop_touch_activate_view, d);

	//////////////////////////////////////////////////////////
	//switch workspace
	const struct tw_key_press *switch_ws_left =
		taiwins_config_get_builtin_binding(c, TW_SWITCH_WS_LEFT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_right =
		taiwins_config_get_builtin_binding(c, TW_SWITCH_WS_RIGHT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_back =
		taiwins_config_get_builtin_binding(c, TW_SWITCH_WS_RECENT_BINDING)->keypress;
	tw_bindings_add_key(bindings, switch_ws_left, desktop_workspace_switch,
			    SWITCH_WS_LEFT, d);
	tw_bindings_add_key(bindings, switch_ws_right, desktop_workspace_switch,
			    SWITCH_WS_RIGHT, d);
	tw_bindings_add_key(bindings, switch_ws_back, desktop_workspace_switch_recent,
			   0, d);

	//////////////////////////////////////////////////////////
	//resize view
	const struct tw_key_press *resize_left =
		taiwins_config_get_builtin_binding(c, TW_RESIZE_ON_LEFT_BINDING)->keypress;
	const struct tw_key_press *resize_right =
		taiwins_config_get_builtin_binding(c, TW_RESIZE_ON_RIGHT_BINDING)->keypress;
	tw_bindings_add_key(bindings, resize_left, desktop_view_resize, RESIZE_LEFT, d);
	tw_bindings_add_key(bindings, resize_right, desktop_view_resize, RESIZE_RIGHT, d);

	//////////////////////////////////////////////////////////
	//toggle views
	const struct tw_key_press *toggle_vertical =
		taiwins_config_get_builtin_binding(c, TW_TOGGLE_VERTICAL_BINDING)->keypress;
	const struct tw_key_press *toggle_floating =
		taiwins_config_get_builtin_binding(c, TW_TOGGLE_FLOATING_BINDING)->keypress;
	const struct tw_key_press *next_view =
		taiwins_config_get_builtin_binding(c, TW_NEXT_VIEW_BINDING)->keypress;
	const struct tw_key_press *vsplit =
		taiwins_config_get_builtin_binding(c, TW_VSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *hsplit =
		taiwins_config_get_builtin_binding(c, TW_HSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *merge =
		taiwins_config_get_builtin_binding(c, TW_MERGE_BINDING)->keypress;

	tw_bindings_add_key(bindings, toggle_vertical, desktop_toggle_vertical, 0, d);
	tw_bindings_add_key(bindings, toggle_floating, desktop_toggle_floating, 0, d);
	tw_bindings_add_key(bindings, next_view, desktop_recent_view, 0, d);
	tw_bindings_add_key(bindings, vsplit, desktop_split_view, 0, d);
	tw_bindings_add_key(bindings, hsplit, desktop_split_view, 1, d);
	tw_bindings_add_key(bindings, merge, desktop_merge_view, 0, d);
}
