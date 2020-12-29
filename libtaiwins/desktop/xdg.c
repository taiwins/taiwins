/*
 * xdg.c - taiwins desktop shell implementation
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
#include <stdint.h>
#include <string.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/seat.h>
#include <taiwins/engine.h>
#include <taiwins/output_device.h>
#include <taiwins/render_output.h>
#include <taiwins/xdg.h>
#include <taiwins/shell.h>

#include "workspace.h"
#include "xdg_internal.h"
#include "layout.h"

static struct tw_xdg s_desktop = {0};

static inline struct tw_xdg_output *
xdg_output_from_engine_output(struct tw_xdg *xdg,
                              struct tw_engine_output *output)
{
	return &xdg->outputs[output->id];
}

/******************************************************************************
 * tw_desktop_surface_api
 *****************************************************************************/

static inline bool
twdesk_view_should_map_immediately(struct tw_xdg_view *view)
{
	return view->type != LAYOUT_FLOATING;
}

static inline struct tw_workspace *
twdesk_view_find_workspace(struct tw_xdg *xdg, struct tw_xdg_view *view)
{
	struct tw_workspace *ws = NULL;
	for (unsigned i = 0; i < MAX_WORKSPACES; i++) {
		ws = &xdg->workspaces[i];
		if (tw_workspace_has_view(ws, view))
			return ws;
	}
	return NULL;
}

static void
twdesk_surface_focus(struct tw_xdg *xdg, struct tw_desktop_surface *dsurf)
{
	struct tw_seat *tw_seat;
	struct tw_engine_seat *seat =
		tw_engine_get_focused_seat(xdg->engine);
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *view;

	if (!seat) {
		tw_logl("no seat available!");
		return;
	}
	tw_seat = seat->tw_seat;
	if (tw_seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		tw_keyboard_set_focus(&tw_seat->keyboard,
		                      dsurf->tw_surface->resource, NULL);
	dsurf->ping(dsurf, wl_display_next_serial(tw_seat->display));
	wl_list_for_each(view, &ws->recent_views, link)
		tw_xdg_view_set_focus(view, (view->dsurf == dsurf));
}

static void
twdesk_refocus(void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *next = wl_list_empty(&ws->recent_views) ? NULL :
		container_of(ws->recent_views.next, struct tw_xdg_view, link);
	if (next)
		twdesk_surface_focus(xdg, next->dsurf);
}

static void
twdesk_ping_timeout(struct tw_desktop_surface *client,
                    void *user_data)
{
}
static void
twdesk_pong(struct tw_desktop_surface *client,
            void *user_data)
{}

static void
twdesk_surface_added(struct tw_desktop_surface *dsurf, void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *view = tw_xdg_view_create(dsurf);
	struct tw_engine_output *output =
		tw_engine_get_focused_output(xdg->engine);
	struct tw_xdg_output *xdg_output;

	if (!view) {
		tw_logl("unable to map desktop surface@%s",
		        dsurf->title);
		return;
	}
	//maybe mapping view now
	dsurf->user_data = view;
	view->type = ws->current_layout;
	if (twdesk_view_should_map_immediately(view)) {
		xdg_output = xdg_output_from_engine_output(xdg, output);
		view->output = xdg_output;
		tw_workspace_add_view(ws, view);
	}
}

static void
twdesk_surface_removed(struct tw_desktop_surface *dsurf,
		       void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_xdg_view *view = dsurf->user_data;
	struct tw_workspace *ws = twdesk_view_find_workspace(xdg, view);
	struct wl_event_loop *loop = wl_display_get_event_loop(xdg->display);

	if (!view) {
		tw_logl("desktop surface@%s is not a xdg surface",
			dsurf->title);
		return;
	}
	if (view->added) {
		assert(ws);
		tw_workspace_remove_view(ws, view);
	}
	tw_xdg_view_destroy(view);
	dsurf->user_data = NULL;
	//This is a HACK, because we are in a wl_resource_destroy_listener,
	//refocus the keyboard will reference the deleted wl_surface. Here we
	//have to wait a idle event for keyboard the clear the focus itself.
	wl_event_loop_add_idle(loop, twdesk_refocus, xdg);
}

static void
twdesk_surface_committed(struct tw_desktop_surface *dsurf,
                         void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_xdg_view *view = dsurf->user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_engine_output *output =
		tw_engine_get_focused_output(xdg->engine);

	if (!view->added) {
		view->output = xdg_output_from_engine_output(xdg, output);
		tw_workspace_add_view(ws, view);
	} else {
		tw_xdg_view_set_position(view, view->x, view->y);
	}

	if (!view->mapped) {
		twdesk_surface_focus(xdg, dsurf);
		view->mapped = true;
	}
}

static void
twdesk_surface_show_window_menu(struct tw_desktop_surface *surface,
                                struct wl_resource *seat,
                                int32_t x, int32_t y,
                                void *user_data)
{
	//TODO implementation
	tw_logl("desktop_surface@%d requires show window menu",
	        wl_resource_get_id(surface->resource));
}

static void
twdesk_set_parent(struct tw_desktop_surface *surface,
                  struct tw_desktop_surface *parent,
                  void *user_data)
{
	//TODO implementation
	tw_logl("desktop_surface@%d requires set_parent",
	        wl_resource_get_id(surface->resource));
}

static void
twdesk_surface_move(struct tw_desktop_surface *dsurf,
                    struct wl_resource *seat_resource, uint32_t serial,
                    void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);
	struct tw_xdg_view *view = dsurf->user_data;

	assert(view);
	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("serial not matched for desktop moving grab.");
		return;
	}
	if (!tw_xdg_start_moving_grab(xdg, view, seat)) {
		tw_logl("failed to start desktop moving grab.");
		return;
	}
}

static void
twdesk_surface_resize(struct tw_desktop_surface *dsurf,
                      struct wl_resource *seat_resource, uint32_t serial,
                      enum wl_shell_surface_resize edge, void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);
	struct tw_xdg_view *view = dsurf->user_data;

	assert(view);
	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("serial not matched for desktop moving grab.");
		return;
	}
	if (!tw_xdg_start_resizing_grab(xdg, view, edge, seat)) {
		tw_logl("failed to start desktop moving grab.");
		return;
	}
}

static void
twdesk_fullscreen(struct tw_desktop_surface *dsurf,
                  struct wl_resource *output_resource, bool fullscreen,
                  void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_engine_output *engine_output = output_resource ?
		tw_engine_output_from_resource(xdg->engine, output_resource) :
		NULL;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *view = dsurf->user_data;
	struct tw_xdg_output *xdg_output;

        assert(view);
	xdg_output = engine_output ?
		xdg_output_from_engine_output(xdg, engine_output) :
		view->output;
	assert(xdg_output);

        if (tw_workspace_has_view(ws, view)) {
	        tw_workspace_fullscreen_view(ws, view, xdg_output, fullscreen);
	        twdesk_surface_focus(xdg, dsurf);
        }
}


static void
twdesk_maximized(struct tw_desktop_surface *dsurf, bool maximized,
                 void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *view = dsurf->user_data;

	assert(view);
	if (tw_workspace_has_view(ws, view)) {
		tw_workspace_maximize_view(ws, view, maximized);
	        twdesk_surface_focus(xdg, dsurf);
	}
}

static void
twdesk_minimized(struct tw_desktop_surface *dsurf, void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_xdg_view *view = dsurf->user_data;
	if (!tw_workspace_has_view(ws, view))
		return;
	tw_workspace_minimize_view(ws, view);
}

static const struct tw_desktop_surface_api desktop_impl =  {
	.ping_timeout = twdesk_ping_timeout,
	.pong = twdesk_pong,
	.surface_added = twdesk_surface_added,
	.surface_removed = twdesk_surface_removed,
	.committed = twdesk_surface_committed,
	.set_parent = twdesk_set_parent,
	.show_window_menu = twdesk_surface_show_window_menu,
	.move = twdesk_surface_move,
	.resize = twdesk_surface_resize,
	.fullscreen_requested = twdesk_fullscreen,
	.maximized_requested = twdesk_maximized,
	.minimized_requested = twdesk_minimized,
};
/******************************************************************************
 * listeners
 *****************************************************************************/


static void
handle_desktop_output_create(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output = data;
	struct tw_xdg *desktop =
		container_of(listener, struct tw_xdg, output_create_listener);

	struct tw_xdg_output *xdg_output = &desktop->outputs[output->id];
	xdg_output->xdg = desktop;
	xdg_output->output = output;
	xdg_output->idx = output->id;
	xdg_output->desktop_area = (desktop->shell) ?
		tw_shell_output_available_space(desktop->shell, output) :
		tw_output_device_geometry(output->device);

	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_add_output(&desktop->workspaces[i], xdg_output);


}

static void
handle_desktop_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xdg *desktop =
		wl_container_of(listener, desktop, output_destroy_listener);
	struct tw_xdg_output *xdg_output =
		xdg_output_from_engine_output(desktop, data);

        for (int i = 0; i < MAX_WORKSPACES; i++) {
		struct tw_workspace *w = &desktop->workspaces[i];
		tw_workspace_remove_output(w, xdg_output);
	}
}


static void
handle_desktop_area_change(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output = data;
	struct tw_xdg *desktop =
		container_of(listener, struct tw_xdg, desktop_area_listener);
	struct tw_xdg_output *xdg_output = &desktop->outputs[output->id];

        xdg_output->desktop_area =
	        tw_shell_output_available_space(desktop->shell, output);
	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_resize_output(&desktop->workspaces[i],
		                           xdg_output);
}

static void
end_desktop(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_xdg *d = container_of(listener, struct tw_xdg,
					 display_destroy_listener);

	tw_reset_wl_list(&d->output_create_listener.link);
	tw_reset_wl_list(&d->desktop_area_listener.link);

	for (int i = 0; i < MAX_WORKSPACES; i++) {
		tw_workspace_release(&d->workspaces[i]);
		tw_xdg_layout_end_tiling(&d->tiling_layouts[i]);
	}

        tw_xdg_layout_end_floating(&d->floating_layout);
	tw_xdg_layout_end_maximized(&d->floating_layout);
	tw_xdg_layout_init_fullscreen(&d->fullscreen_layout);
}

static void
init_desktop_listeners(struct tw_xdg *xdg)
{
	wl_list_init(&xdg->display_destroy_listener.link);
	wl_list_init(&xdg->desktop_area_listener.link);
	wl_list_init(&xdg->output_create_listener.link);

	//install signals
	tw_set_display_destroy_listener(xdg->display,
	                                &xdg->display_destroy_listener,
	                                end_desktop);
	tw_signal_setup_listener(&xdg->engine->events.output_created,
	                         &xdg->output_create_listener,
	                         handle_desktop_output_create);
	tw_signal_setup_listener(&xdg->engine->events.output_remove,
	                         &xdg->output_destroy_listener,
	                         handle_desktop_output_destroy);
	if (xdg->shell)
		tw_signal_setup_listener(
			tw_shell_get_desktop_area_signal(xdg->shell),
			&xdg->desktop_area_listener,
			handle_desktop_area_change);
}

static void
init_desktop_workspaces(struct tw_xdg *xdg)
{
	struct tw_workspace *wss = xdg->workspaces;

	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_init(&wss[i], &xdg->engine->layers_manager, i);
	xdg->actived_workspace[0] = &wss[0];
	xdg->actived_workspace[1] = &wss[1];
	tw_workspace_switch(xdg->actived_workspace[0],
	                    xdg->actived_workspace[0]);
}

static void
init_desktop_layouts(struct tw_xdg *xdg)
{
	//does not have layers now
	tw_xdg_layout_init_floating(&xdg->floating_layout);
	tw_xdg_layout_init_maximized(&xdg->maximized_layout);
	tw_xdg_layout_init_fullscreen(&xdg->fullscreen_layout);

	for (int i = 0; i < MAX_WORKSPACES; i++) {
		wl_list_insert(xdg->workspaces[i].layouts.prev,
		               &xdg->floating_layout.links[i]);
		wl_list_insert(xdg->workspaces[i].layouts.prev,
		               &xdg->maximized_layout.links[i]);
		wl_list_insert(xdg->workspaces[i].layouts.prev,
		               &xdg->fullscreen_layout.links[i]);
		//tiling
		tw_xdg_layout_init_tiling(&xdg->tiling_layouts[i]);
		wl_list_insert(xdg->workspaces[i].layouts.prev,
		               &xdg->tiling_layouts[i].links[i]);
	}
}

/******************************************************************************
 * APIs
 *****************************************************************************/
WL_EXPORT void
tw_xdg_view_activate(struct tw_xdg *xdg, struct tw_xdg_view *view)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];
	assert(tw_workspace_has_view(ws, view));
	tw_workspace_focus_view(ws, view);
	twdesk_surface_focus(xdg, view->dsurf);
}

WL_EXPORT void
tw_xdg_toggle_view_split(struct tw_xdg *xdg, struct tw_xdg_view *view)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];

        assert(tw_workspace_has_view(ws, view));
        tw_workspace_run_command(ws, DPSR_toggle, view);
}

WL_EXPORT void
tw_xdg_toggle_view_layout(struct tw_xdg *xdg, struct tw_xdg_view *view)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];

        assert(tw_workspace_has_view(ws, view));
	tw_workspace_remove_view(ws, view);
	//toggle tiling
	if (view->type == LAYOUT_TILING) {
		if (view->prev_type != LAYOUT_TILING)
			view->type = view->prev_type;
		else
			view->type = LAYOUT_FLOATING;
		view->prev_type = LAYOUT_TILING;
	} else {
		view->prev_type = view->type;
		view->type = LAYOUT_TILING;
	}

	tw_workspace_add_view(ws, view);
}

WL_EXPORT void
tw_xdg_split_on_view(struct tw_xdg *xdg, struct tw_xdg_view *view,
                     bool vsplit)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];
        assert(tw_workspace_has_view(ws, view));
        tw_workspace_run_command(ws, vsplit ? DPSR_vsplit : DPSR_hsplit,
                                 view);
}

WL_EXPORT void
tw_xdg_merge_view(struct tw_xdg *xdg, struct tw_xdg_view *view)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];

        assert(tw_workspace_has_view(ws, view));
        tw_workspace_run_command(ws, DPSR_merge, view);
}

WL_EXPORT void
tw_xdg_resize_view(struct tw_xdg *xdg, struct tw_xdg_view *view,
                   int32_t dx, int32_t dy, enum wl_shell_surface_resize edge)
{
	struct tw_workspace *ws = xdg->actived_workspace[0];

        assert(tw_workspace_has_view(ws, view));
        tw_workspace_resize_view(ws, view, dx, dy, edge);
}

WL_EXPORT int
tw_xdg_current_workspace_idx(struct tw_xdg *xdg)
{
	return xdg->actived_workspace[0]->idx;
}

WL_EXPORT int
tw_xdg_last_workspace_idx(struct tw_xdg *xdg)
{
	return xdg->actived_workspace[1]->idx;
}

WL_EXPORT void
tw_xdg_set_workspace_layout(struct tw_xdg *xdg, int32_t idx,
                            enum tw_layout_type layout)
{
	if (idx < MAX_WORKSPACES)
		tw_workspace_switch_layout(&xdg->workspaces[idx], layout);
}

WL_EXPORT void
tw_xdg_switch_workspace(struct tw_xdg *xdg, uint32_t to)
{
	struct tw_xdg_view *view;
	struct tw_engine_output *eo;
	struct tw_render_output *rd;
	struct tw_xdg_output *xo;

	assert(to < MAX_WORKSPACES);
	xdg->actived_workspace[1] = xdg->actived_workspace[0];
	xdg->actived_workspace[0] = &xdg->workspaces[to];
	view = tw_workspace_switch(xdg->actived_workspace[0],
	                           xdg->actived_workspace[1]);
	//enforcing a output-resizing event here to enforce the changed
	//xdg options like gaps.
	wl_list_for_each(eo, &xdg->engine->heads, link) {
		xo = xdg_output_from_engine_output(xdg, eo);
		tw_workspace_resize_output(xdg->actived_workspace[0], xo);
		rd = wl_container_of(eo->device, rd, device);
		tw_render_output_dirty(rd);
	}
	if (view)
		tw_xdg_view_activate(xdg, view);
}

WL_EXPORT const char *
tw_xdg_workspace_layout_name(struct tw_xdg *xdg, uint32_t i)
{
	struct tw_workspace *ws;
	if (i >= MAX_WORKSPACES)
		return NULL;
	ws = &xdg->workspaces[i];

	switch (ws->current_layout) {
	case LAYOUT_FLOATING:
		return "floating";
	case LAYOUT_TILING:
		return "tiling";
	case LAYOUT_MAXIMIZED:
		return "maximized";
	case LAYOUT_FULLSCREEN:
		return "fullscreened";
	}
	return NULL;
}

WL_EXPORT int
tw_xdg_layout_type_from_name(const char *name)
{
	if (name && !strcmp(name, "floating"))
		return LAYOUT_FLOATING;
	else if (name && !strcmp(name, "tiling"))
		return LAYOUT_TILING;
	else if (name && !strcmp(name, "maximized"))
		return LAYOUT_MAXIMIZED;
	else if (name && !strcmp(name, "fullscreened"))
		return LAYOUT_FULLSCREEN;
	return -1;
}

WL_EXPORT void
tw_xdg_set_desktop_gap(struct tw_xdg *xdg, uint32_t igap, uint32_t ogap)
{
	struct tw_engine_output *eo;
	struct tw_xdg_output *xo;

	for (int i = 0; i < 32; i++) {
		xdg->outputs[i].inner_gap = igap;
		xdg->outputs[i].outer_gap = ogap;
	}
	//here we only chage the apply to the current workspace, the rest
	//workspaces applied at `tw_xdg_switch_workspace`.
	wl_list_for_each(eo, &xdg->engine->heads, link) {
		xo = xdg_output_from_engine_output(xdg, eo);
		tw_workspace_resize_output(xdg->actived_workspace[0], xo);
	}

}

WL_EXPORT struct tw_xdg *
tw_xdg_create_global(struct wl_display *display, struct tw_shell *shell,
                     struct tw_engine *engine)
{
	struct tw_xdg *desktop = &s_desktop;
	if (desktop->display) {
		tw_logl_level(TW_LOG_ERRO, "desktop already initialized.");
		return NULL;
	}
	//initialize the desktop
	desktop->display = display;
	desktop->shell = shell;
	desktop->engine = engine;
	for (int i = 0; i < 32; i++) {
		desktop->outputs[i].inner_gap = 10;
		desktop->outputs[i].outer_gap = 10;
	}

	if (!tw_desktop_init(&desktop->desktop_manager, display,
	                     &desktop_impl, desktop,
	                     TW_DESKTOP_INIT_INCLUDE_WL_SHELL |
	                     TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL))
		return NULL;

	init_desktop_listeners(desktop);
	init_desktop_workspaces(desktop);
	init_desktop_layouts(desktop);

	///getting the xwayland API now, xwayland module has to load at this
	///point, we would need the API here to deal with xwayland surface, it
	///it is not available, it must mean we do not deal with xwayland here
	/* desktop->xwayland_api = weston_xwayland_surface_get_api(ec); */
	return &s_desktop;
}
