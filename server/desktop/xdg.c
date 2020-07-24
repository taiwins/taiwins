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

#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <objects/surface.h>
#include <objects/logger.h>
#include <objects/utils.h>
#include <objects/layers.h>
#include <objects/desktop.h>
#include <objects/seat.h>
#include <wayland-util.h>

#include "backend/backend.h"
#include "shell.h"
#include "xdg.h"
#include "workspace.h"



struct grab_interface {
	union {
		struct tw_seat_pointer_grab pointer_grab;
		struct tw_seat_touch_grab touch_grab;
		struct tw_seat_keyboard_grab keyboard_grab;
	};
	/* need this struct to access the workspace */
	struct tw_surface *surface;
};

static struct tw_xdg {
	struct wl_display *display;
	struct tw_shell *shell;
	struct tw_backend *backend;

        struct tw_desktop_manager desktop_manager;

	/* managing current status */
	struct tw_workspace *actived_workspace[2];
	struct tw_workspace workspaces[9];

	struct wl_listener desktop_area_listener;
	struct wl_listener display_destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener surface_transform_listener;

	/**< grabs */
	struct grab_interface moving_grab;
	struct grab_interface resizing_grab;
	struct grab_interface task_switch_grab;

        /**< params */
	int32_t inner_gap, outer_gap;
} s_desktop = {0};

/******************************************************************************
 * grab interface apis
 *****************************************************************************/

static inline void
grab_interface_init(struct grab_interface *gi,
		    const struct tw_pointer_grab_interface *pi,
		    const struct tw_keyboard_grab_interface *ki,
		    const struct tw_touch_grab_interface *ti)
{
	if (pi)
		gi->pointer_grab.impl = pi;
	else if (ki)
		gi->keyboard_grab.impl = ki;
	else if (ti)
		gi->touch_grab.impl = ti;
}

/******************************************************************************
 * tw_desktop_surface_api
 *****************************************************************************/

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
	struct tw_xdg_view *view = tw_xdg_view_create(dsurf);
	if (!view) {
		tw_logl("unable to map desktop surface@%s",
		        dsurf->title);
		return;
	}
	dsurf->user_data = view;
}

static void
twdesk_surface_removed(struct tw_desktop_surface *dsurf,
		       void *user_data)
{
	struct tw_surface *tw_surface = dsurf->tw_surface;
	struct tw_xdg_view *view = dsurf->user_data;

	tw_reset_wl_list(&tw_surface->links[TW_VIEW_LAYER_LINK]);
	if (!view) {
		tw_logl("desktop surface@%s is not a xdg surface",
			dsurf->title);
		return;
	}
	tw_xdg_view_destroy(view);
	dsurf->user_data = NULL;
}

static void
twdesk_surface_committed(struct tw_desktop_surface *dsurf,
                         void *user_data)
{
	struct tw_xdg *xdg = user_data;
	struct tw_xdg_view *view = dsurf->user_data;
	struct tw_workspace *ws = xdg->actived_workspace[0];

	if (!view->mapped) {
		view->type = LAYOUT_FLOATING;
		tw_workspace_add_view(ws, view);
		/* int32_t x = bo->state.x + bo->state.w/2 - */
		/*	dsurf->window_geometry.w/2; */
		/* int32_t y = bo->state.y + bo->state.h/2 - */
		/*	dsurf->window_geometry.h/2; */
		/* tw_xdg_view_set_position(view, x, y); */
		view->mapped = true;
	}
}

static void
twdesk_surface_show_window_menu(struct tw_desktop_surface *surface,
                                struct wl_resource *seat,
                                int32_t x, int32_t y,
                                void *user_data)
{}

static void
twdesk_set_parent(struct tw_desktop_surface *surface,
                  struct tw_desktop_surface *parent,
                  void *user_data)
{
}

static void
twdesk_surface_move(struct tw_desktop_surface *surface,
            struct wl_resource *seat, uint32_t serial,
            void *user_data)
{}

static void
twdesk_surface_resize(struct tw_desktop_surface *surface,
              struct wl_resource *seat, uint32_t serial,
              enum wl_shell_surface_resize edges, void *user_data)
{
}

static void
twdesk_fullscreen(struct tw_desktop_surface *surface,
                            struct wl_resource *output,
                            bool fullscreen, void *user_data)
{
}

static void
twdesk_maximized(struct tw_desktop_surface *surface,
                           bool maximized, void *user_data)
{
}

static void
twdesk_minimized(struct tw_desktop_surface *surface,
                           void *user_data)
{}

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
	struct tw_backend_output *output = data;
	struct tw_xdg *desktop =
		container_of(listener, struct tw_xdg, output_create_listener);

	struct tw_xdg_output xdg_output = {
		.output = output,
		.desktop_area = tw_shell_output_available_space(
			desktop->shell, output),
		.inner_gap = desktop->inner_gap,
		.outer_gap = desktop->outer_gap,
	};
	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_add_output(&desktop->workspaces[i], &xdg_output);
}

static void
handle_desktop_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_backend_output *output = data;
	struct tw_xdg *desktop =
		container_of(listener, struct tw_xdg, output_destroy_listener);
        for (int i = 0; i < MAX_WORKSPACES; i++) {
		struct tw_workspace *w = &desktop->workspaces[i];
		struct tw_xdg_output xdg_output = {.output = output, };

		tw_workspace_remove_output(w, &xdg_output);
	}
}

static void
handle_desktop_area_change(struct wl_listener *listener, void *data)
{
	struct tw_backend_output *output = data;
	struct tw_xdg *desktop =
		container_of(listener, struct tw_xdg, desktop_area_listener);
	struct tw_xdg_output xdg_output = {
		.output = output,
		.desktop_area = tw_shell_output_available_space(
			desktop->shell, output),
		.inner_gap = desktop->inner_gap,
		.outer_gap = desktop->outer_gap,
	};

	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_resize_output(&desktop->workspaces[i],
		                           &xdg_output);
}

static void
end_desktop(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_xdg *d = container_of(listener, struct tw_xdg,
					 display_destroy_listener);

	tw_reset_wl_list(&d->output_create_listener.link);
	tw_reset_wl_list(&d->output_destroy_listener.link);
	tw_reset_wl_list(&d->desktop_area_listener.link);

	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_release(&d->workspaces[i]);
}

static void
init_desktop_listeners(struct tw_xdg *xdg)
{
	//install signals
	tw_set_display_destroy_listener(xdg->display,
	                                &xdg->display_destroy_listener,
	                                end_desktop);
	tw_signal_setup_listener(&xdg->backend->output_plug_signal,
	                         &xdg->output_create_listener,
	                         handle_desktop_output_create);
	tw_signal_setup_listener(&xdg->backend->output_unplug_signal,
	                         &xdg->output_destroy_listener,
	                         handle_desktop_output_destroy);
	tw_signal_setup_listener(tw_shell_get_desktop_area_signal(xdg->shell),
	                         &xdg->desktop_area_listener,
	                         handle_desktop_area_change);
}

static void
init_desktop_workspaces(struct tw_xdg *xdg)
{
	struct tw_workspace *wss = xdg->workspaces;

	for (int i = 0; i < MAX_WORKSPACES; i++)
		tw_workspace_init(&wss[i], xdg->backend, i);
	xdg->actived_workspace[0] = &wss[0];
	xdg->actived_workspace[1] = &wss[1];
	tw_workspace_switch(xdg->actived_workspace[0],
	                    xdg->actived_workspace[0]);
}

/******************************************************************************
 * APIs
 *****************************************************************************/

struct tw_xdg *
tw_xdg_create_global(struct wl_display *display, struct tw_shell *shell,
                     struct tw_backend *backend)
{
	struct tw_xdg *desktop = &s_desktop;
	if (desktop->display) {
		tw_logl_level(TW_LOG_ERRO, "desktop already initialized.");
		return NULL;
	}
	//initialize the desktop
	desktop->display = display;
	desktop->shell = shell;
	desktop->backend = backend;
	//default params
	desktop->inner_gap = 10;
	desktop->outer_gap = 10;
	/* tw_layer_init(&desktop->desktop_layer); */
	/* wl_list_init(&desktop->recent_views); */
	/* tw_layer_set_position(&desktop->desktop_layer, */
	/*                       TW_LAYER_POS_DESKTOP_MID, */
	/*                       &backend->layers_manager); */

	if (!tw_desktop_init(&desktop->desktop_manager, display,
	                     &desktop_impl, desktop,
	                     TW_DESKTOP_INIT_INCLUDE_WL_SHELL |
	                     TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL))
		return NULL;

	init_desktop_listeners(desktop);
	init_desktop_workspaces(desktop);


	/* desktop->api = weston_desktop_create(ec, &desktop_impl, &s_desktop); */
	//install grab
	/* grab_interface_init(&desktop->moving_grab, */
	/*		    &desktop_moving_grab, NULL, NULL); */
	/* grab_interface_init(&desktop->resizing_grab, */
	/*		    &desktop_resizing_grab, NULL, NULL); */
	/* grab_interface_init(&desktop->task_switch_grab, */
	/*		    NULL, &desktop_task_switch_grab, NULL); */
	/* grab_interface_init(&desktop->alpha_grab, */
	/*                     &desktop_alpha_grab, NULL, NULL); */

	//TODO desktop resize listener, well, we can do this with
	//desktop_area_changed

	///getting the xwayland API now, xwayland module has to load at this
	///point, we would need the API here to deal with xwayland surface, it
	///it is not available, it must mean we do not deal with xwayland here
	/* desktop->xwayland_api = weston_xwayland_surface_get_api(ec); */
	return &s_desktop;
}
