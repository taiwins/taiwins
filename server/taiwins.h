/*
 * taiwins.h - taiwins server shared header
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

#ifndef TAIWINS_H
#define TAIWINS_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctypes/helpers.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <libweston/libweston.h>

#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_data_device.h>
#include <objects/logger.h>
#include <objects/layers.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include <objects/subprocess.h>
#include <objects/dmabuf.h>

#include "seat/seat.h"
#include "backend/backend.h"
#include "input.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define MAX_WORKSPACE 8

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_server {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */
	struct tw_backend *backend;

	/* wlr datas */
	struct wlr_backend *wlr_backend;
	struct wlr_renderer *wlr_renderer;
	struct wlr_compositor *wlr_compositor;
	struct wlr_data_device_manager *wlr_data_device;
	struct tw_bindings *binding_state;

	/* seats */
	struct tw_seat_events seat_events[8];
	struct wl_listener seat_add;
	struct wl_listener seat_remove;

	/* compositor/surface/buffer functions */
	struct tw_compositor *compositor;
	struct tw_linux_dmabuf *dma_engine;
	struct tw_surface_manager surface_manager;
	struct wl_listener surface_create_listener;
	struct wl_listener subsurface_create_listener;
	struct wl_listener region_create_listener;
};

bool
tw_server_init(struct tw_server *server, struct wl_display *display);


int
tw_handle_sigchld(int sig_num, void *data);

int
tw_term_on_signal(int sig_num, void *data);

bool
tw_set_socket(struct wl_display *display);

/*******************************************************************************
 * desktop functions
 ******************************************************************************/

void
tw_lose_surface_focus(struct weston_surface *surface);

void
tw_focus_surface(struct weston_surface *surface);

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor);

/******************************************************************************
 * util functions
 *****************************************************************************/
/*
static inline struct weston_output *
tw_get_default_output(struct weston_compositor *compositor)
{
        if (wl_list_empty(&compositor->output_list))
                return NULL;
        return container_of(compositor->output_list.next,
                            struct weston_output, link);
}

static inline struct weston_seat *
tw_get_default_seat(struct weston_compositor *ec)
{
        if (wl_list_empty(&ec->seat_list))
                return NULL;
        //be careful with container of, it doesn't care the
        return container_of(ec->seat_list.next,
                            struct weston_seat, link);
}

static inline struct weston_surface *
tw_surface_from_resource(struct wl_resource *wl_surface)
{
        return (struct weston_surface *)wl_resource_get_user_data(wl_surface);
}

static inline struct weston_view *
tw_default_view_from_surface(struct weston_surface *surface)
{
        //return NULL if no view is
        return (surface->views.next != &surface->views) ?

                container_of(surface->views.next, struct weston_view,
surface_link) : NULL;
}

static inline struct weston_view *
tw_view_from_surface_resource(struct wl_resource *wl_surface)
{
        return tw_default_view_from_surface(
                tw_surface_from_resource(wl_surface));
}

static inline void
tw_map_view(struct weston_view *view)
{
        view->surface->is_mapped = true;
        view->is_mapped = true;
}
*/

/******************************************************************************
 * util functions
 *****************************************************************************/


bool
tw_set_wl_surface(struct wl_client *client,
                  struct wl_resource *resource,
                  struct wl_resource *surface,
                  struct wl_resource *output,
                  struct wl_listener *surface_destroy_listener);

//two option to manipulate the view
void
setup_static_view(struct weston_view *view, struct weston_layer *layer,
                  int x, int y);
void
setup_ui_view(struct weston_view *view, struct weston_layer *layer,
              int x, int y);

/******************************************************************************
 * libweston interface functions
 *****************************************************************************/

void *
tw_load_weston_module(const char *name, const char *entrypoint);

//the declarations we need to move back
void
weston_output_move(struct weston_output *output, int x, int y);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
