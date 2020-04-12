/*
 * desktop.c - taiwins desktop implementation
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wayland-server.h>
#include <strops.h>
#include <helpers.h>
#include <sequential.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <libweston-desktop/libweston-desktop.h>
#include <libweston/xwayland-api.h>
#include <libweston/libweston.h>

#define INCLUDE_DESKTOP
#include "../../shared_config.h"
#include "../taiwins.h"
#include "../config.h"
#include "shell.h"
#include "layout.h"
#include "workspace.h"

#define MAX_WORKSPACE 8

/**
 * grab decleration, with different options.
 */
struct grab_interface {
	union {
		struct weston_pointer_grab pointer_grab;
		struct weston_touch_grab touch_grab;
		struct weston_keyboard_grab keyboard_grab;
	};
	/* need this struct to access the workspace */
	struct weston_view *view;
};

typedef OPTION(unsigned int, value) gap_option_t;

static struct desktop {
	struct weston_compositor *compositor;
	struct shell *shell;
	/* managing current status */
	struct workspace *actived_workspace[2];
	struct workspace workspaces[9];
	struct weston_desktop *api;
	const struct weston_xwayland_surface_api *xwayland_api;

	struct wl_listener desktop_area_listener;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_resize_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener surface_transform_listener;
	struct tw_apply_bindings_listener add_binding;
	struct tw_config_component_listener config_component;
	//grabs
	struct grab_interface moving_grab;
	struct grab_interface resizing_grab;
	struct grab_interface task_switch_grab;
	//params
	gap_option_t inner_gap, outer_gap;
	enum taiwins_shell_task_switch_effect ts_effect;
} s_desktop;

struct desktop *
tw_desktop_get_global() {return &s_desktop; }

/*******************************************************************************
 * desktop API
 ******************************************************************************/

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

static inline void
desktop_set_worksace_layout(struct desktop *d, unsigned int i,
                            enum layout_type type)
{
	struct workspace *w;

	if (i > MAX_WORKSPACE)
		return;
	w = &d->workspaces[i];
	w->current_layout = type;
}

/*******************************************************************************
 * grab interface apis
 ******************************************************************************/
static inline void
grab_interface_init(struct grab_interface *gi,
		    const struct weston_pointer_grab_interface *pi,
		    const struct weston_keyboard_grab_interface *ki,
		    const struct weston_touch_grab_interface *ti)
{
	if (pi)
		gi->pointer_grab.interface = pi;
	else if (ki)
		gi->keyboard_grab.interface = ki;
	else if (ti)
		gi->touch_grab.interface = ti;
}

static inline void
grab_interface_fini(struct grab_interface *gi)
{
	gi->view = NULL;
}

static inline void
grab_interface_start_pointer(struct grab_interface *gi, struct weston_view *v,
			     struct weston_seat *seat)
{
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);

	if (gi->view)
		return;
	gi->view = v;
	gi->pointer_grab.pointer = pointer;
	if (pointer && pointer->grab == &pointer->default_grab) {
		weston_pointer_start_grab(pointer, &gi->pointer_grab);
	}
}

static inline void
grab_interface_start_keyboard(struct grab_interface *gi, struct weston_view *v,
			      struct weston_seat *seat)
{
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);

	if (gi->view)
		return;
	gi->view = v;
	gi->keyboard_grab.keyboard = keyboard;
	if (keyboard && keyboard->grab == &keyboard->default_grab)
		weston_keyboard_start_grab(keyboard, &gi->keyboard_grab);
}

/*******************************************************************************
 * libwestop desktop implementaiton
 ******************************************************************************/

/*
 * here we are facing this problem, desktop_view has an additional geometry. The
 * content within that geometry is visible. This geometry is only available at
 * commit. AND IT CAN CHANGE
 *
 * what we can do is that we map this view into an offscreen space, then remap
 * it back. When on the commit, we can have them back?
 */
static inline bool
is_view_on_desktop(const struct weston_view *v, const struct desktop *desk)
{
	return is_view_on_workspace(v, desk->actived_workspace[0]);
}

static void
twdesk_ping_timeout(struct weston_desktop_client *client,
		    void *user_data)
{
	//struct desktop *d = user_data;
	//shell change cursor?
}

static void
twdesk_pong(struct weston_desktop_client *client,
	    void *user_data)
{}

static void
twdesk_surface_added(struct weston_desktop_surface *surface,
		     void *user_data)
{
	enum layout_type layout;
	struct workspace *wsp = NULL;
	struct recent_view *rv = NULL;
	struct desktop *desktop = user_data;
	//remove old view (if any) and create one
	struct weston_view *view, *next;
	struct weston_surface *wt_surface =
		weston_desktop_surface_get_surface(surface);


	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_desktop_surface_create_view(surface);
	wl_list_init(&view->link);
	view->is_mapped = true;
	wt_surface->is_mapped = true;
	weston_desktop_surface_set_activated(surface, true);
	view->output = tw_get_focused_output(wt_surface->compositor);
	if (!view->output)
		view->output = tw_get_default_output(wt_surface->compositor);
	wt_surface->output = view->output;

	//creating recent view
	wsp = desktop->actived_workspace[0];
	//if xwayland, we will add it to floating surface
	if (desktop->xwayland_api &&
	    desktop->xwayland_api->is_xwayland_surface(wt_surface))
		layout = LAYOUT_FLOATING;
	else
		layout = wsp->current_layout;

	rv = recent_view_create(view, layout);
	weston_desktop_surface_set_user_data(surface, rv);

	workspace_add_view(wsp, view);
	tw_focus_surface(wt_surface);
}

static void
twdesk_surface_removed(struct weston_desktop_surface *surface,
		       void *user_data)
{
	struct desktop *desktop = user_data;
	struct weston_surface *wt_surface =
		weston_desktop_surface_get_surface(surface);
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
	wl_list_for_each(rv, &ws->recent_views, link)
		if (rv->view->layer_link.layer != &ws->hidden_layer) {
			workspace_focus_view(ws, rv->view);
			tw_focus_surface(rv->view->surface);
			break;
		}
}

static void
twdesk_surface_committed(struct weston_desktop_surface *desktop_surface,
			 int32_t sx, int32_t sy, void *data)
{
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct recent_view *rv =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_view *view =
		container_of(surface->views.next, struct weston_view,
		             surface_link);
	struct weston_geometry geo =
		weston_desktop_surface_get_geometry(desktop_surface);
	//check the current surface geometry
	if (geo.x != rv->visible_geometry.x || geo.y != rv->visible_geometry.y) {
		float x, y;
		recent_view_get_origin_coord(rv, &x, &y);
		weston_view_set_position(view, x - geo.x, y - geo.y);
		weston_view_geometry_dirty(view);
		rv->visible_geometry = geo;
	}
	weston_view_damage_below(view);
	weston_view_schedule_repaint(view);
}

static void
twdesk_surface_move(struct weston_desktop_surface *desktop_surface,
		    struct weston_seat *seat, uint32_t serial, void *user_data)
{
	struct desktop *desktop = user_data;
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = tw_default_view_from_surface(surface);
	if (pointer && pointer->focus && pointer->button_count > 0)
		grab_interface_start_pointer(&desktop->moving_grab, view, seat);
}

static void
twdesk_surface_resize(struct weston_desktop_surface *desktop_surface,
		      struct weston_seat *seat, uint32_t serial,
		      enum weston_desktop_surface_edge edges, void *user_data)
{
	struct desktop *desktop = user_data;
	struct weston_pointer *pointer =
		weston_seat_get_pointer(seat);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view =
		tw_default_view_from_surface(surface);
	if (pointer && pointer->focus && pointer->button_count > 0)
		grab_interface_start_pointer(
			&desktop->resizing_grab, view, seat);
}

static void
twdesk_fullscreen(struct weston_desktop_surface *surface,
		  bool fullscreen,
		  struct weston_output *output,
		  void *user_data)
{
	struct desktop *d = user_data;
	struct weston_surface *weston_surface =
		weston_desktop_surface_get_surface(surface);
	struct workspace *ws = d->actived_workspace[0];
	struct weston_view *view =
		tw_default_view_from_surface(weston_surface);
	workspace_fullscreen_view(ws, view, fullscreen);
}

static void
twdesk_maximized(struct weston_desktop_surface *surface,
		 bool maximized, void *user_data)
{
	struct desktop *d = user_data;
	struct workspace *ws = d->actived_workspace[0];
	struct weston_surface *weston_surface =
		weston_desktop_surface_get_surface(surface);
	struct weston_view *view =
		tw_default_view_from_surface(weston_surface);
	workspace_maximize_view(ws, view, maximized);
}

static void
twdesk_minimized(struct weston_desktop_surface *surface,
		 void *user_data)
{
	struct desktop *d = user_data;
	struct weston_surface *weston_surface =
		weston_desktop_surface_get_surface(surface);
	struct workspace *ws = d->actived_workspace[0];
	struct weston_view *view =
		tw_default_view_from_surface(weston_surface);
		workspace_minimize_view(ws, view);
}

static void
twdesk_xwayland_pos(struct weston_desktop_surface *surface,
                    int32_t x, int32_t y, void *user_data)
{
	struct recent_view *rv;
	struct desktop *d = user_data;
	struct workspace *ws = d->actived_workspace[0];

	if (d->xwayland_api) {
		rv = weston_desktop_surface_get_user_data(surface);
		rv->xwayland.is_xwayland = true;
		rv->xwayland.x = x;
		rv->xwayland.y = y;

		//if it is not on the workspace, we pending this work later
		if (!is_view_on_workspace(rv->view, ws))
			return;
		if (rv->type != LAYOUT_FLOATING)
			workspace_switch_layout(ws, rv->view);
	}
}

static struct weston_desktop_api desktop_impl =  {
	.ping_timeout = twdesk_ping_timeout,
	.pong = twdesk_pong,
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.move = twdesk_surface_move,
	.resize = twdesk_surface_resize,
	.fullscreen_requested = twdesk_fullscreen,
	.maximized_requested = twdesk_maximized,
	.minimized_requested = twdesk_minimized,
	.set_xwayland_position = twdesk_xwayland_pos,
	//set_xwayland_position
	.struct_size = sizeof(struct weston_desktop_api),
};

/*******************************************************************************
 * desktop.surface listener
 ******************************************************************************/
static void
desktop_surface_transformed(struct wl_listener *listener, void *data)
{
	int x, y;
	struct weston_view *view;
	struct recent_view *rv;
	struct weston_surface *surface = data;
	struct weston_desktop_surface *desktop_surface;

	struct desktop *desktop =
		container_of(listener, struct desktop,
		             surface_transform_listener);
	if (!weston_surface_is_desktop_surface(surface))
		return;
	desktop_surface = weston_surface_get_desktop_surface(surface);
	rv = weston_desktop_surface_get_user_data(desktop_surface);

	//this could be pop-up window in window in X that does not have any
	//information.
	if (!rv) {
		struct weston_seat *default_seat  =
			tw_get_default_seat(surface->compositor);
		struct weston_pointer *default_pointer =
			weston_seat_get_pointer(default_seat);
		if (!default_pointer)
			return;

		x = wl_fixed_to_int(default_pointer->x);
		y = wl_fixed_to_int(default_pointer->y);
		view = tw_default_view_from_surface(surface);
	} else {
		view = rv->view;
		x = rv->view->geometry.x;
		y = rv->view->geometry.y;
	}

	if (desktop->xwayland_api && weston_view_is_mapped(view))
		desktop->xwayland_api->send_position(surface, x, y);
}

/*******************************************************************************
 * desktop.output listener
 ******************************************************************************/

static void
desktop_output_created(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct desktop *desktop =
		container_of(listener, struct desktop, output_create_listener);

	struct tw_output tw_output = {
		.output = output,
		.desktop_area = shell_output_available_space(
			desktop->shell, output),
		.inner_gap = desktop->inner_gap.value,
		.outer_gap = desktop->outer_gap.value,
	};
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_add_output(&desktop->workspaces[i], &tw_output);
}

static void
desktop_output_resized(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct desktop *desktop =
		container_of(listener, struct desktop, output_resize_listener);

	struct tw_output tw_output = {
		.output = output,
		.desktop_area = shell_output_available_space(
			desktop->shell, output),
		.inner_gap = desktop->inner_gap.value,
		.outer_gap = desktop->outer_gap.value,
	};
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_resize_output(&desktop->workspaces[i], &tw_output);
}

static void
desktop_output_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct desktop *desktop =
		container_of(listener, struct desktop, output_destroy_listener);

	for (int i = 0; i < MAX_WORKSPACE+1; i++) {
		struct workspace *w = &desktop->workspaces[i];
		//you somehow need to move the views to other output
		workspace_remove_output(w, output);
	}
}

static void
desktop_area_changed(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct desktop *desktop =
		container_of(listener, struct desktop, desktop_area_listener);
	struct tw_output tw_output = {
		.output = output,
		.desktop_area = shell_output_available_space(
			desktop->shell, output),
		.inner_gap = desktop->inner_gap.value,
		.outer_gap = desktop->outer_gap.value,
	};
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_resize_output(&desktop->workspaces[i], &tw_output);
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
alpha_axis(struct weston_pointer *pointer,
           struct weston_pointer_axis_event *event,
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

static void noop_grab_axis(struct weston_pointer_grab *grab,
                           const struct timespec *time,
                           struct weston_pointer_axis_event *event ) {}

static void noop_grab_axis_source(struct weston_pointer_grab *grab,
                                  uint32_t source) {}

static void noop_grab_frame(struct weston_pointer_grab *grab) {}

static void
move_grab_pointer_motion(struct weston_pointer_grab *grab,
			 const struct timespec *time,
			 struct weston_pointer_motion_event *event)
{
	double dx, dy;
	struct grab_interface *gi = container_of(grab, struct grab_interface,
	                                         pointer_grab);
	struct desktop *d = container_of(gi, struct desktop, moving_grab);

	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;
	//so this function will have no effect on tiling views
	//you don't need to arrange view here
	workspace_move_view(ws, gi->view, &(struct weston_position) {
			gi->view->geometry.x + dx, gi->view->geometry.y + dy});
}

static void
resize_grab_pointer_motion(struct weston_pointer_grab *grab,
			   const struct timespec *time,
			   struct weston_pointer_motion_event *event)
{
	double dx, dy;
	/* struct weston_position pos; */
	struct grab_interface *gi =
		container_of(grab, struct grab_interface, pointer_grab);
	struct desktop *d = container_of(gi, struct desktop,
					 resizing_grab);

	struct workspace *ws = d->actived_workspace[0];
	//this func change the pointer->x pointer->y
	pointer_motion_delta(grab->pointer, event, &dx, &dy);
	weston_pointer_move(grab->pointer, event);
	if (!gi->view)
		return;

	workspace_resize_view(ws, gi->view,
			      grab->pointer->x, grab->pointer->y,
			      dx, dy);
}

/*
static void
noop_grab_pointer_motion(struct weston_pointer_grab *grab, const struct timespec *time,
			 struct weston_pointer_motion_event *event)
{
	weston_pointer_move(grab->pointer, event);
}
*/

static void
pointer_grab_cancel(struct weston_pointer_grab *grab)
{
	//an universal implemention, destroy the grab all the time
	struct grab_interface *gi = container_of(grab, struct grab_interface,
	                                         pointer_grab);
	weston_pointer_end_grab(grab->pointer);
	grab_interface_fini(gi);
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


/*************************************************
 * keybaord grab
 ************************************************/
static void
task_switch_grab_key(struct weston_keyboard_grab *grab,
		     const struct timespec *time, uint32_t key, uint32_t state)
{
	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		grab->interface->cancel(grab);
		return;
	}
	struct grab_interface *gi = container_of(grab, struct grab_interface,
						 keyboard_grab);
	struct desktop *d = container_of(gi, struct desktop, task_switch_grab);
	struct workspace *w = d->actived_workspace[0];
	struct wl_list *link = w->recent_views.next;
	struct wl_array tosent;
	struct tw_window_brief *brief;

	wl_array_init(&tosent);
	wl_array_add(&tosent, sizeof(struct tw_window_brief) *
		     wl_list_length(&w->recent_views));
	//build the list of recent views.
	wl_array_for_each(brief, &tosent) {
		struct recent_view *rv =
			container_of(link, struct recent_view, link);
		struct weston_desktop_surface *surface =
			weston_surface_get_desktop_surface(rv->view->surface);
		strop_ncpy(brief->name,
		           weston_desktop_surface_get_title(surface),
		           sizeof(brief->name));
		recent_view_get_origin_coord(rv, &brief->x, &brief->y);
		brief->w = rv->view->surface->width;
		brief->h = rv->view->surface->height;
		link = link->next;
	}
	shell_post_data(d->shell, TAIWINS_SHELL_MSG_TYPE_TASK_SWITCHING,
			&tosent);
	wl_array_release(&tosent);

}

static void
task_switch_grab_modifier(struct weston_keyboard_grab *grab,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched,
			  uint32_t mods_locked, uint32_t group)
{}

static void
task_switch_grab_cancel(struct weston_keyboard_grab *grab)
{
	struct grab_interface *gi =
		container_of(grab, struct grab_interface,
		             keyboard_grab);
	struct desktop *d = container_of(gi, struct desktop,
	                                 task_switch_grab);

	shell_post_message(d->shell,
	                   TAIWINS_SHELL_MSG_TYPE_SWITCH_WORKSPACE,
	                   "");
	weston_keyboard_end_grab(grab->keyboard);
	grab_interface_fini(gi);
}

static struct weston_keyboard_grab_interface desktop_task_switch_grab = {
	.key = task_switch_grab_key,
	.modifiers = task_switch_grab_modifier,
	.cancel = task_switch_grab_cancel,
};

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

/***************************************************************
 * binding callbacks
 **************************************************************/

static void
desktop_alpha_axis(struct weston_pointer *pointer,
		   const struct timespec *time,
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
		   const struct timespec *time,
		   uint32_t button, void *data)
{
	struct weston_seat *seat = pointer->seat;
	struct weston_view *view = pointer->focus;
	struct desktop *desktop = data;
	if (pointer->button_count > 0 && view &&
	    is_view_on_desktop(view, desktop) &&
	    pointer->grab == &pointer->default_grab)
		grab_interface_start_pointer(&desktop->moving_grab, view, seat);
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
	if (!pointer->focus || !pointer->button_count ||
	    !weston_surface_is_desktop_surface(pointer->focus->surface))
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
	if (!touch->focus ||
	    !weston_surface_is_desktop_surface(touch->focus->surface))
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
			 uint32_t key, uint32_t switch_left,
			 void *data)
{
	struct desktop *desktop = data;
	struct workspace *ws = desktop->actived_workspace[0];
	off_t ws_idx = get_workspace_index(ws, desktop);
	desktop->actived_workspace[1] = ws;
	if (switch_left == true)
		ws_idx = MAX(0, ws_idx-1);
	else
		ws_idx = MIN(MAX_WORKSPACE, ws_idx+1);
	desktop->actived_workspace[0] = &desktop->workspaces[ws_idx];
	workspace_switch(&desktop->workspaces[ws_idx], ws, keyboard);
	//send msgs, those type of message
	char msg[32];
	snprintf(msg, 32, "%d", (uint32_t)ws_idx);
	shell_post_message(desktop->shell,
			   TAIWINS_SHELL_MSG_TYPE_SWITCH_WORKSPACE,
	                   msg);
}


static void
desktop_workspace_switch_recent(struct weston_keyboard *keyboard,
				const struct timespec *time,
				uint32_t key, uint32_t option,
				void *data)
{
	struct desktop *desktop = data;
	SWAP(desktop->actived_workspace[0], desktop->actived_workspace[1]);
	workspace_switch(desktop->actived_workspace[0], desktop->actived_workspace[1],
	                 keyboard);
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
	if (!weston_surface_is_desktop_surface(keyboard->focus))
		return;
	//as a keybinding, we only operate on the lower button of the view
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	float dx = 0.0, dy = 0.0, x = 0.0, y = 0.0;
	switch (option) {
	case RESIZE_LEFT:
		dx = -10;
		break;
	case RESIZE_RIGHT:
		dx = 10;
		break;
	case RESIZE_UP:
		dy = -10;
		break;
	case RESIZE_DOWN:
		dy = 10;
		break;
	default:
		dx = 10;
	}
	weston_view_to_global_float(view, view->surface->width-1,
				    view->surface->height-1,
				    &x, &y);
	workspace_resize_view(ws, view,
			      wl_fixed_from_double(x),
			      wl_fixed_from_double(y), dx, dy);
}

static void
desktop_toggle_vertical(struct weston_keyboard *keyboard,
			const struct timespec *time, uint32_t key,
			uint32_t option, void *data)
{
	if (!keyboard->focus ||
	    !weston_surface_is_desktop_surface(keyboard->focus))
		return;
	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	if (ws)
		workspace_view_run_command(ws, view, DPSR_toggle);
}

static void
desktop_toggle_floating(struct weston_keyboard *keyboard,
			const struct timespec *time, uint32_t key,
			uint32_t option, void *data)
{
	if (!keyboard->focus ||
	    !weston_surface_is_desktop_surface(keyboard->focus))
		return;

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
	if (!keyboard->focus ||
	    !weston_surface_is_desktop_surface(keyboard->focus))
		return;

	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	if (ws)
		workspace_view_run_command(
			ws, view, (option == 0) ? DPSR_vsplit : DPSR_hsplit);
}

static void
desktop_merge_view(struct weston_keyboard *keyboard,
		   const struct timespec *time, uint32_t key,
		   uint32_t option, void *data)
{
	if (!keyboard->focus ||
	    !weston_surface_is_desktop_surface(keyboard->focus))
		return;

	struct desktop *desktop = data;
	struct weston_view *view = tw_default_view_from_surface(keyboard->focus);
	struct workspace *ws = get_workspace_for_view(view, desktop);
	if (ws)
		workspace_view_run_command(ws, view, DPSR_merge);
}

static void
desktop_recent_view(struct weston_keyboard *keyboard,
		    const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	//you should always be in you active workspace
	struct desktop *desktop = data;
	struct workspace *ws = desktop->actived_workspace[0];
	struct recent_view *rv, *tmp;
	wl_list_for_each_safe(rv, tmp, &ws->recent_views, link) {
		//move view to the back
		wl_list_remove(&rv->link);
		wl_list_insert(ws->recent_views.prev, &rv->link);
		rv = &tmp->link != &ws->recent_views ? tmp : rv;
		//start the grab now
		grab_interface_start_keyboard(&desktop->task_switch_grab,
					      rv->view, keyboard->seat);
		//and we need run the key as well.
		keyboard->grab->interface->key(
			keyboard->grab,time, key,
			WL_KEYBOARD_KEY_STATE_PRESSED);

		workspace_focus_view(ws, rv->view);
		tw_focus_surface(rv->view->surface);
		break;
	}
}

static bool
desktop_add_bindings(struct tw_bindings *bindings, struct tw_config *c,
		     struct tw_apply_bindings_listener *listener)
{
	struct desktop *d = container_of(listener, struct desktop, add_binding);
	bool safe = true;
	//////////////////////////////////////////////////////////
	//move press
	struct tw_btn_press move_press =
		tw_config_get_builtin_binding(c, TW_MOVE_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &move_press, desktop_click_move, d);
	//////////////////////////////////////////////////////////
	//transparent
	struct tw_axis_motion axis_motion =
		tw_config_get_builtin_binding(c, TW_ALPHA_AXIS_BINDING)->axisaction;
	tw_bindings_add_axis(bindings, &axis_motion, desktop_alpha_axis, d);

	//////////////////////////////////////////////////////////
	//focus press
	struct tw_btn_press focus_press =
		tw_config_get_builtin_binding(c, TW_FOCUS_PRESS_BINDING)->btnpress;
	tw_bindings_add_btn(bindings, &focus_press,
			    desktop_click_activate_view, d);
	tw_bindings_add_touch(bindings, 0, desktop_touch_activate_view, d);

	//////////////////////////////////////////////////////////
	//switch workspace
	const struct tw_key_press *switch_ws_left =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_LEFT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_right =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_RIGHT_BINDING)->keypress;

	const struct tw_key_press *switch_ws_back =
		tw_config_get_builtin_binding(c, TW_SWITCH_WS_RECENT_BINDING)->keypress;
	safe = safe && tw_bindings_add_key(bindings, switch_ws_left,
					   desktop_workspace_switch,
					   true, //switch to left
					   d);
	safe = safe && tw_bindings_add_key(bindings, switch_ws_right,
					   desktop_workspace_switch,
					   false, //switch to right
					   d);
	safe = safe && tw_bindings_add_key(bindings, switch_ws_back, desktop_workspace_switch_recent,
					 0, d);

	//////////////////////////////////////////////////////////
	//resize view
	const struct tw_key_press *resize_left =
		tw_config_get_builtin_binding(c, TW_RESIZE_ON_LEFT_BINDING)->keypress;
	const struct tw_key_press *resize_right =
		tw_config_get_builtin_binding(c, TW_RESIZE_ON_RIGHT_BINDING)->keypress;
	safe = safe &&
		tw_bindings_add_key(bindings, resize_left, desktop_view_resize, RESIZE_LEFT, d);
	safe = safe &&
		tw_bindings_add_key(bindings, resize_right, desktop_view_resize, RESIZE_RIGHT, d);

	//////////////////////////////////////////////////////////
	//toggle views
	const struct tw_key_press *toggle_vertical =
		tw_config_get_builtin_binding(c, TW_TOGGLE_VERTICAL_BINDING)->keypress;
	const struct tw_key_press *toggle_floating =
		tw_config_get_builtin_binding(c, TW_TOGGLE_FLOATING_BINDING)->keypress;
	const struct tw_key_press *next_view =
		tw_config_get_builtin_binding(c, TW_NEXT_VIEW_BINDING)->keypress;
	const struct tw_key_press *vsplit =
		tw_config_get_builtin_binding(c, TW_VSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *hsplit =
		tw_config_get_builtin_binding(c, TW_HSPLIT_WS_BINDING)->keypress;
	const struct tw_key_press *merge =
		tw_config_get_builtin_binding(c, TW_MERGE_BINDING)->keypress;

	safe = safe && tw_bindings_add_key(bindings, toggle_vertical, desktop_toggle_vertical, 0, d);
	safe = safe && tw_bindings_add_key(bindings, toggle_floating, desktop_toggle_floating, 0, d);
	safe = safe && tw_bindings_add_key(bindings, next_view, desktop_recent_view, 0, d);
	safe = safe && tw_bindings_add_key(bindings, vsplit, desktop_split_view, 0, d);
	safe = safe && tw_bindings_add_key(bindings, hsplit, desktop_split_view, 1, d);
	safe = safe && tw_bindings_add_key(bindings, merge, desktop_merge_view, 0, d);

	return safe;
}

static void
desktop_apply_config(struct tw_config *c, bool cleanup,
                     struct tw_config_component_listener *listener)
{
	struct weston_output *output;
	struct desktop *d =
		container_of(listener, struct desktop, config_component);

	if (d->inner_gap.valid || d->outer_gap.valid) {
		wl_list_for_each(output, &d->compositor->output_list, link)
			desktop_output_resized(&d->output_resize_listener,
			                       output);
		d->inner_gap.valid = false;
		d->outer_gap.valid = false;
	}

}

static void
end_desktop(struct wl_listener *listener, void *data)
{
	struct desktop *d = container_of(listener, struct desktop,
					 compositor_destroy_listener);

	wl_list_remove(&d->output_create_listener.link);
	wl_list_remove(&d->output_destroy_listener.link);
	for (int i = 0; i < MAX_WORKSPACE+1; i++)
		workspace_release(&d->workspaces[i]);
	weston_desktop_destroy(d->api);
}

/*******************************************************************************
 * public API
 ******************************************************************************/
int
tw_desktop_num_workspaces(struct desktop *desktop)
{
	return MAX_WORKSPACE;
}

const char *
tw_desktop_get_workspace_layout(struct desktop *desktop, unsigned int i)
{
	return (i > MAX_WORKSPACE) ? NULL :
		workspace_layout_name(&desktop->workspaces[i]);
}

bool
tw_desktop_set_workspace_layout(struct desktop *desktop, unsigned int i,
                                const char *layout)
{
	bool ret = true;
	if (i > MAX_WORKSPACE)
		ret = false;
	else if (!strcmp(layout, "float"))
		desktop_set_worksace_layout(desktop, i, LAYOUT_FLOATING);
	else if (!strcmp(layout, "tiling"))
		desktop_set_worksace_layout(desktop, i, LAYOUT_TILING);
	else
		ret = false;
	return ret;
}
void
tw_desktop_get_gap(struct desktop *desktop, int *inner, int *outer)
{
	*inner = desktop->inner_gap.value;
	*outer = desktop->outer_gap.value;
}

void
tw_desktop_set_gap(struct desktop *desktop, int inner, int outer)
{
	desktop->inner_gap.valid = true;
	desktop->inner_gap.value = inner;
	desktop->outer_gap.valid = true;
	desktop->outer_gap.value = outer;
}

bool
tw_setup_desktop(struct weston_compositor *ec,  struct tw_config *config)
{
	//initialize the desktop
	s_desktop.compositor = ec;
	s_desktop.shell = tw_shell_get_global();
	//default params
	s_desktop.inner_gap.value = 10;
	s_desktop.outer_gap.value = 10;
	s_desktop.inner_gap.valid = false;
	s_desktop.outer_gap.valid = false;

	struct workspace *wss = s_desktop.workspaces;
	{
		for (int i = 0; i < MAX_WORKSPACE+1; i++)
			workspace_init(&wss[i], ec);
		s_desktop.actived_workspace[0] = &wss[0];
		s_desktop.actived_workspace[1] = &wss[1];
		workspace_switch(s_desktop.actived_workspace[0],
		                 s_desktop.actived_workspace[0], NULL);
	}
	s_desktop.api = weston_desktop_create(ec, &desktop_impl, &s_desktop);
	//setup listeners
	struct weston_output *output;
	//install grab
	grab_interface_init(&s_desktop.moving_grab,
			    &desktop_moving_grab, NULL, NULL);
	grab_interface_init(&s_desktop.resizing_grab,
			    &desktop_resizing_grab, NULL, NULL);
	grab_interface_init(&s_desktop.task_switch_grab,
			    NULL, &desktop_task_switch_grab, NULL);
	//install signals
	wl_list_init(&s_desktop.desktop_area_listener.link);
	wl_list_init(&s_desktop.output_create_listener.link);
	wl_list_init(&s_desktop.output_destroy_listener.link);
	wl_list_init(&s_desktop.output_resize_listener.link);
	wl_list_init(&s_desktop.surface_transform_listener.link);

	s_desktop.desktop_area_listener.notify = desktop_area_changed;
	s_desktop.output_create_listener.notify = desktop_output_created;
	s_desktop.output_destroy_listener.notify = desktop_output_destroyed;
	s_desktop.output_resize_listener.notify = desktop_output_resized;
	s_desktop.surface_transform_listener.notify = desktop_surface_transformed;

	//add existing output
	shell_add_desktop_area_listener(tw_shell_get_global(),
	                          &s_desktop.desktop_area_listener);
	wl_signal_add(&ec->output_created_signal,
		      &s_desktop.output_create_listener);
	wl_signal_add(&ec->output_resized_signal,
		      &s_desktop.output_resize_listener);
	wl_signal_add(&ec->output_destroyed_signal,
		      &s_desktop.output_destroy_listener);
	wl_signal_add(&ec->transform_signal,
	              &s_desktop.surface_transform_listener);

	wl_list_for_each(output, &ec->output_list, link)
		desktop_output_created(&s_desktop.output_create_listener,
				       output);
	//install bindings
	wl_list_init(&s_desktop.add_binding.link);
	s_desktop.add_binding.apply = desktop_add_bindings;
	tw_config_add_apply_bindings(config, &s_desktop.add_binding);

	wl_list_init(&s_desktop.config_component.link);
	s_desktop.config_component.apply = desktop_apply_config;
	tw_config_add_component(config, &s_desktop.config_component);

	wl_list_init(&s_desktop.compositor_destroy_listener.link);
	s_desktop.compositor_destroy_listener.notify = end_desktop;
	wl_signal_add(&ec->destroy_signal,
		      &s_desktop.compositor_destroy_listener);

	///getting the xwayland API now, xwayland module has to load at this
	///point, we would need the API here to deal with xwayland surface, it
	///it is not available, it must mean we do not deal with xwayland here
	s_desktop.xwayland_api = weston_xwayland_surface_get_api(ec);
	return true;
}
