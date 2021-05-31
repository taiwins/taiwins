/*
 * xdg_grab.c - taiwins desktop grabs
 *
 * Copyright (c)  Xichen Zhou
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

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>
#include "xdg.h"

#include "xdg_grab.h"
#include "workspace.h"


/******************************************************************************
 * grab_interface API
 *****************************************************************************/

static void
tw_xdg_grab_interface_destroy(struct tw_xdg_grab_interface *gi)
{
	wl_list_remove(&gi->view_destroy_listener.link);
	if (gi->idle_motion_source)
		wl_event_source_remove(gi->idle_motion_source);
	free(gi);
}

static void
notify_grab_interface_view_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xdg_grab_interface *gi =
		container_of(listener, struct tw_xdg_grab_interface,
		             view_destroy_listener);
	assert(data == gi->view);
	if (gi->pointer_grab.impl)
		tw_pointer_end_grab(&gi->pointer_grab.seat->pointer,
		                    &gi->pointer_grab);
	else if (gi->keyboard_grab.impl)
		tw_keyboard_end_grab(&gi->keyboard_grab.seat->keyboard,
		                     &gi->keyboard_grab);
	else if (gi->touch_grab.impl)
		tw_touch_end_grab(&gi->touch_grab.seat->touch,
		                  &gi->touch_grab);
}

static struct tw_xdg_grab_interface *
tw_xdg_grab_interface_create(struct tw_xdg_view *view, struct tw_xdg *xdg,
                             const struct tw_pointer_grab_interface *pi,
                             const struct tw_keyboard_grab_interface *ki,
                             const struct tw_touch_grab_interface *ti)
{
	struct tw_xdg_grab_interface *gi = calloc(1, sizeof(*gi));
	if (!gi)
		return NULL;
	if (pi) {
		gi->pointer_grab.impl = pi;
		gi->pointer_grab.data = gi;
	} else if (ki) {
		gi->keyboard_grab.impl = ki;
		gi->keyboard_grab.data = gi;
	} else if (ti) {
		gi->touch_grab.impl = ti;
		gi->touch_grab.data = gi;
	}
	gi->gx = nanf("");
	gi->gy = nanf("");
	gi->view = view;
	gi->xdg = xdg;
	tw_signal_setup_listener(&view->dsurf_umapped_signal,
	                         &gi->view_destroy_listener,
	                         notify_grab_interface_view_destroy);
	return gi;
}

static void
tw_xdg_grab_interface_add_idle_motion(struct tw_xdg_grab_interface *gi,
                                      wl_event_loop_idle_func_t func)
{
	if (!gi->idle_motion_source) {
		struct tw_xdg *xdg = gi->xdg;
		struct wl_display *display = xdg->display;
		struct wl_event_loop *loop = wl_display_get_event_loop(display);

		gi->idle_motion_source = wl_event_loop_add_idle(loop, func, gi);
	}
}

/******************************************************************************
 * pointer moving grab
 *****************************************************************************/

static void
idle_move(void *data)
{
	struct tw_xdg_grab_interface *gi = data;
	struct tw_xdg *xdg = gi->xdg;
	struct tw_workspace *ws = xdg->actived_workspace[0];

	if (gi->dx != 0.0f || gi->dy != 0.0f)
		tw_workspace_move_view(ws, gi->view, gi->dx, gi->dy);
	gi->dx = 0.0f;
	gi->dy = 0.0f;
	//destroy the event_source when we are done.
	gi->idle_motion_source = NULL;
}

static void
handle_move_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct tw_xdg_grab_interface *gi =
		container_of(grab, struct tw_xdg_grab_interface, pointer_grab);
	struct tw_surface *surf = gi->view->dsurf->tw_surface;
	float gx, gy;
	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);

	//TODO: when we set position for the view, here we immedidately changed
	//its position. flickering may caused from that. The cursor is fine.
	if (!isnan(gi->gx) && !isnan(gi->gy)) {
		gi->dx += gx - gi->gx;
		gi->dy += gy - gi->gy;
		tw_xdg_grab_interface_add_idle_motion(gi, idle_move);
	}
	gi->gx = gx;
	gi->gy = gy;
}

static void
handle_move_pointer_grab_button(struct tw_seat_pointer_grab *grab,
	                   uint32_t time_msec, uint32_t button,
	                   enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
	    pointer->btn_count == 0)
		tw_pointer_end_grab(pointer, grab);
}

static void
handle_move_pointer_grab_cancel(struct tw_seat_pointer_grab *grab)
{
	struct tw_xdg_grab_interface *gi = grab->data;
	tw_xdg_grab_interface_destroy(gi);
}

static const struct tw_pointer_grab_interface move_pointer_grab_impl = {
	.motion = handle_move_pointer_grab_motion,
	.button = handle_move_pointer_grab_button,
	.cancel = handle_move_pointer_grab_cancel,
};

/******************************************************************************
 * pointer moving grab
 *****************************************************************************/

static void
idle_resize(void *data)
{
	struct tw_xdg_grab_interface *gi = data;
	struct tw_xdg *xdg = gi->xdg;
	struct tw_workspace *ws = xdg->actived_workspace[0];

	if (gi->dx != 0.0f || gi->dy != 0.0f)
		tw_workspace_resize_view(ws, gi->view, gi->dx, gi->dy,
		                         gi->edge);
	gi->dx = 0.0f;
	gi->dy = 0.0f;
	//destroy the event_source when we are done.
	gi->idle_motion_source = NULL;
}


static void
handle_resize_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct tw_xdg_grab_interface *gi =
		container_of(grab, struct tw_xdg_grab_interface, pointer_grab);
	struct tw_surface *surf = gi->view->dsurf->tw_surface;
	float gx, gy;

	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);

	if (!isnan(gi->gx) && !isnan(gi->gy)) {
		gi->dx += gx - gi->gx;
		gi->dy += gy - gi->gy;
		tw_xdg_grab_interface_add_idle_motion(gi, idle_resize);
	}
	gi->gx = gx;
	gi->gy = gy;
}

static const struct tw_pointer_grab_interface resize_pointer_grab_impl = {
	.motion = handle_resize_pointer_grab_motion,
	.button = handle_move_pointer_grab_button, //same as move grab
	.cancel = handle_move_pointer_grab_cancel, //same as move grab
};

/******************************************************************************
 * task switching grab
 *****************************************************************************/
static inline bool
find_task_view(struct tw_workspace *ws, struct tw_xdg_view *view)
{
	struct tw_xdg_view *pos;
	wl_list_for_each(pos, &ws->recent_views, link)
		if (pos == view)
			return true;
	return false;
}

static void
handle_task_switching_key(struct tw_seat_keyboard_grab *grab,
                          uint32_t time_msec, uint32_t key, uint32_t state)
{
	struct tw_xdg_grab_interface *gi = grab->data;
	struct tw_xdg *xdg = gi->xdg;
	//it is likely to be the
	struct tw_xdg_view *view = gi->view;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED ||
	    wl_list_empty(&ws->recent_views))
		return;
	if (!find_task_view(ws, view))
		view = container_of(ws->recent_views.next, struct tw_xdg_view,
		                    link);
	//find next view (skip the head).
	gi->view = (view->link.next != &ws->recent_views) ?
		container_of(view->link.next, struct tw_xdg_view, link) :
		container_of(view->link.next->next, struct tw_xdg_view, link);
	//TODO: send clients the current tasks(exposay)
}

static void
handle_task_switching_modifiers(struct tw_seat_keyboard_grab *grab,
                                uint32_t mods_depressed, uint32_t mods_latched,
                                uint32_t mods_locked, uint32_t group)
{
	struct tw_xdg_grab_interface *gi = grab->data;
	struct tw_keyboard *keyboard = &grab->seat->keyboard;
	struct tw_xdg *xdg = gi->xdg;
	struct tw_xdg_view *view = gi->view;
	struct tw_workspace *ws = xdg->actived_workspace[0];

	//modifiers changed, end grab now
	if (keyboard->modifiers_state != gi->mod_mask) {
		if (find_task_view(ws, view))
			tw_xdg_view_activate(xdg, view);
		tw_keyboard_end_grab(keyboard, grab);
	}
}

static void
handle_task_switching_cancel(struct tw_seat_keyboard_grab *grab)
{
	struct tw_xdg_grab_interface *gi = grab->data;
	tw_xdg_grab_interface_destroy(gi);
}

static const struct tw_keyboard_grab_interface task_switching_impl = {
	.enter = tw_keyboard_default_enter,
	.key = handle_task_switching_key,
	.modifiers = handle_task_switching_modifiers,
	.cancel = handle_task_switching_cancel,
};


/******************************************************************************
 * exposed API
 *****************************************************************************/

WL_EXPORT bool
tw_xdg_start_moving_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                         struct tw_seat *seat)
{
	struct tw_xdg_grab_interface *gi = NULL;

	if (seat->pointer.btn_count == 0)
		goto err;
	gi = tw_xdg_grab_interface_create(view, xdg, &move_pointer_grab_impl,
	                                  NULL, NULL);
	if (!gi)
		goto err;
	tw_pointer_start_grab(&seat->pointer, &gi->pointer_grab);
	return true;
err:
	return false;
}

WL_EXPORT bool
tw_xdg_start_resizing_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                           enum wl_shell_surface_resize edge,
                           struct tw_seat *seat)
{
	struct tw_xdg_grab_interface *gi = NULL;
	if (seat->pointer.btn_count == 0) {
		goto err;
	}
	gi = tw_xdg_grab_interface_create(view, xdg, &resize_pointer_grab_impl,
	                                  NULL, NULL);
	if (!gi)
		goto err;
	gi->edge = edge;
	tw_pointer_start_grab(&seat->pointer, &gi->pointer_grab);
	return true;
err:
	return false;
}

WL_EXPORT bool
tw_xdg_start_task_switching_grab(struct tw_xdg *xdg, uint32_t time,
                                 uint32_t key, uint32_t modifiers_state,
                                 struct tw_seat *seat)
{
	struct tw_xdg_view *view;
	struct tw_xdg_grab_interface *gi = NULL;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	if (wl_list_empty(&ws->recent_views))
		goto err;
	view = container_of(ws->recent_views.next, struct tw_xdg_view, link);
	gi = tw_xdg_grab_interface_create(view, xdg, NULL,
	                                  &task_switching_impl, NULL);
	if (!gi)
		goto err;
	gi->mod_mask = modifiers_state;
	tw_keyboard_start_grab(&seat->keyboard, &gi->keyboard_grab);
	//immediately jump in next view since, we are missing one key event.
	tw_keyboard_notify_key(&seat->keyboard, time, key,
	                       WL_KEYBOARD_KEY_STATE_PRESSED);
	return true;
err:
	return false;
}
