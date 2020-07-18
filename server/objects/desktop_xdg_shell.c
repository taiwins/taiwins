/*
 * destkop.c - taiwins xdg_shell implementation
 *
 * Copyright (c) 2020 Xichen Zhou
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
#include <string.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wayland-xdg-shell-server-protocol.h>
#include <ctypes/helpers.h>

#include "objects/seat.h"
#include "pixman.h"
#include "utils.h"
#include "desktop.h"
#include "logger.h"
#include "surface.h"
#include "popup_grab.h"

#define XDG_SHELL_VERSION 2

struct tw_xdg_surface {
	struct tw_desktop_surface base;
	struct wl_listener surface_destroy;
	bool configured;
        /* has_next_window geometry once set, will always stay valid, so we can
         * update window geometry every commit. The next_window_geometry is the
         * one set by user, here we will update actual geometry based on it.
         */
	bool has_next_window_geometry;
	pixman_box32_t next_window_geometry;

        union {
		struct {
			struct {
				struct {uint32_t w, h; } min_size, max_size;
			} pending, current;
			struct wl_resource *fullscreen_output;
			struct wl_resource *resource;
		} toplevel;

		struct {
			struct tw_subsurface subsurface;
			struct tw_xdg_surface *parent;
			struct wl_resource *resource;
			struct wl_listener close_popup_listener;
		} popup;
	};
};

struct tw_xdg_positioner {
	struct wl_resource *resource;
	//all kinds of stuff
	struct {
		int width, height;
	} size;
	struct {
		int x, y;
	} offset;
	struct {
		int x, y, width, height;
		enum xdg_positioner_anchor anchor;
	} anchor;
	enum xdg_positioner_gravity gravity;
	uint32_t contraint;
};

static const struct xdg_surface_interface xdg_surface_impl;
static const char *XDG_TOPLEVEL_ROLE_NAME = "XDG_TOPLEVEL";
static const char *XDG_POPUP_ROLE_NAME = "XDG_POPUP";

void
tw_desktop_surface_init(struct tw_desktop_surface *surf,
                        struct wl_resource *wl_surface,
                        struct wl_resource *resource,
                        struct tw_desktop_manager *desktop);
void
tw_desktop_surface_fini(struct tw_desktop_surface *surf);

void
tw_desktop_surface_add(struct tw_desktop_surface *surf);

void
tw_desktop_surface_rm(struct tw_desktop_surface *surf);

void
tw_desktop_surface_set_fullscreen(struct tw_desktop_surface *surf,
                                  struct wl_resource *output,
                                  bool fullscreen);
void
tw_desktop_surface_set_maximized(struct tw_desktop_surface *surf,
                                 bool maximized);
void
tw_desktop_surface_set_title(struct tw_desktop_surface *surf,
                             const char *title);
void
tw_desktop_surface_set_class(struct tw_desktop_surface *surf,
                             const char *class);
void
tw_desktop_surface_move(struct tw_desktop_surface *surf,
                        struct wl_resource *seat, uint32_t serial);
void
tw_desktop_surface_resize(struct tw_desktop_surface *surf,
                          struct wl_resource *seat, uint32_t edge,
                          uint32_t serial);
void
tw_desktop_surface_calc_window_geometry(struct tw_surface *surface,
                                        pixman_region32_t *geometry);

/******************************************************************************
 * xdg_shell_surface implementation
 *****************************************************************************/

static struct tw_xdg_positioner *
positioner_from_resource(struct wl_resource *resource);

static struct tw_desktop_surface *
desktop_surface_from_xdg_surface(struct wl_resource *wl_resource)
{
	assert(wl_resource_instance_of(wl_resource, &xdg_surface_interface,
	                               &xdg_surface_impl));
	return wl_resource_get_user_data(wl_resource);
}

static void
commit_update_window_geometry(struct tw_xdg_surface *xdg_surf)
{
	struct tw_surface *surface = xdg_surf->base.tw_surface;
	struct tw_desktop_surface *dsurf = &xdg_surf->base;
	pixman_region32_t surf_region;
	pixman_box32_t *r;

	pixman_region32_init(&surf_region);
	tw_desktop_surface_calc_window_geometry(surface, &surf_region);
	if (xdg_surf->has_next_window_geometry) {
		r = &xdg_surf->next_window_geometry;
		pixman_region32_intersect_rect(&surf_region, &surf_region,
		                               r->x1,  r->y1,
		                               r->x2 - r->x1,
		                               r->y2 - r->y1);
	}
	r = pixman_region32_extents(&surf_region);
	dsurf->window_geometry.x = r->x1;
	dsurf->window_geometry.y = r->y1;
	dsurf->window_geometry.w = r->x2 - r->x1;
	dsurf->window_geometry.h = r->y2 - r->y1;
	pixman_region32_fini(&surf_region);
}

static void
commit_xdg_toplevel(struct tw_surface *surface)
{
	struct tw_desktop_surface *dsurf = surface->role.commit_private;
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);
	struct tw_desktop_manager *desktop = dsurf->desktop;
	uint32_t id = wl_resource_get_id(dsurf->resource);
	if (!xdg_surf->configured) {
		wl_resource_post_error(dsurf->resource,
		                       XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
		                       "xdg_surface@%d not configured", id);
		return;
	}
	commit_update_window_geometry(xdg_surf);

	xdg_surf->toplevel.current.max_size =
		xdg_surf->toplevel.pending.max_size;
	xdg_surf->toplevel.current.min_size =
		xdg_surf->toplevel.pending.min_size;

	desktop->api.committed(dsurf, desktop->user_data);
}

static void
commit_xdg_popup(struct tw_surface *surface)
{
	struct tw_desktop_surface *dsurf = surface->role.commit_private;
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);

	commit_update_window_geometry(xdg_surf);
}

static bool
is_tw_surface_xdg_surface(struct tw_surface *surface)
{
	return surface->role.commit == commit_xdg_toplevel ||
		surface->role.commit == commit_xdg_popup;
}

static bool
xdg_surface_set_role(struct tw_desktop_surface *dsurf,
                     enum tw_desktop_surface_type type)
{
	struct wl_display *display = dsurf->desktop->display;
	struct tw_surface *surface = dsurf->tw_surface;

	if (type == TW_DESKTOP_TOPLEVEL_SURFACE) {
		if (surface->role.commit &&
		    surface->role.commit != commit_xdg_toplevel)
			return false;
		surface->role.commit = commit_xdg_toplevel;
		surface->role.name = XDG_TOPLEVEL_ROLE_NAME;
	} else if (type == TW_DESKTOP_POPUP_SURFACE) {
		if (surface->role.commit &&
		    surface->role.commit != commit_xdg_popup)
			return false;
		surface->role.commit = commit_xdg_popup;
		surface->role.name = XDG_POPUP_ROLE_NAME;
	} else {
		return false;
	}

	surface->role.commit_private = dsurf;
	dsurf->type = type;
	xdg_surface_send_configure(dsurf->resource,
	                           wl_display_next_serial(display));
	return true;
}

static void
handle_xdg_surf_surface_destroy(struct wl_listener *listener, void *userdata)
{
	struct tw_xdg_surface *surf =
		container_of(listener, struct tw_xdg_surface, surface_destroy);

        tw_desktop_surface_rm(&surf->base);
        tw_reset_wl_list(&surf->surface_destroy.link);
	surf->base.tw_surface = NULL;
}

static void
configure_xdg_surface(struct tw_desktop_surface *dsurf,
                      enum wl_shell_surface_resize edge,
                      int32_t x, int32_t y,
                      unsigned width, unsigned height)
{
	struct tw_xdg_surface *xdg_surface =
		container_of(dsurf, struct tw_xdg_surface, base);
	struct wl_display *display = dsurf->desktop->display;

	if (dsurf->type == TW_DESKTOP_TOPLEVEL_SURFACE)
		xdg_toplevel_send_configure(xdg_surface->toplevel.resource,
		                            width, height, NULL);
	else if (dsurf->type == TW_DESKTOP_POPUP_SURFACE)
		xdg_popup_send_configure(xdg_surface->popup.resource,
		                         x, y, width, height);
	else
		tw_logl_level(TW_LOG_ERRO, "xdg_surface cant be transient");

	xdg_surface_send_configure(dsurf->resource,
	                           wl_display_next_serial(display));
}

static void
close_xdg_surface(struct tw_desktop_surface *dsurf)
{
	struct tw_xdg_surface *xdg_surface =
		container_of(dsurf, struct tw_xdg_surface, base);
	if (dsurf->type == TW_DESKTOP_TOPLEVEL_SURFACE)
		xdg_toplevel_send_close(xdg_surface->toplevel.resource);
	else if (dsurf->type == TW_DESKTOP_POPUP_SURFACE)
		xdg_popup_send_popup_done(xdg_surface->popup.resource);
	else
		tw_logl_level(TW_LOG_ERRO, "xdg_surface cant be transient");
}

static void
init_xdg_surface(struct tw_xdg_surface *surface,
                 struct wl_resource *wl_surface, struct wl_resource *resource,
                 struct tw_desktop_manager *desktop)
{
	tw_desktop_surface_init(&surface->base, wl_surface, resource,
	                        desktop);
	surface->base.configure = configure_xdg_surface;
	surface->base.close = close_xdg_surface;

	tw_set_resource_destroy_listener(wl_surface, &surface->surface_destroy,
	                                 handle_xdg_surf_surface_destroy);
}

/****************************** xdg toplevel *********************************/

static const struct xdg_toplevel_interface toplevel_impl;

static struct tw_xdg_surface *
xdg_surface_from_toplevel(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &xdg_toplevel_interface,
	                               &toplevel_impl));
	return wl_resource_get_user_data(resource);
}

static void
handle_toplevel_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

/* desktop.set_parent */
static void
handle_toplevel_set_parent(struct wl_client *client,
                           struct wl_resource *resource,
			   struct wl_resource *parent_resource)
{
	struct tw_desktop_manager *desktop;
	struct tw_desktop_surface *dsurf, *parent = NULL;

	dsurf = &xdg_surface_from_toplevel(resource)->base;
	if (parent)
		parent = &xdg_surface_from_toplevel(parent_resource)->base;
	desktop = dsurf->desktop;
	desktop->api.set_parent(dsurf, parent, desktop->user_data);

}

static void
handle_toplevel_set_title(struct wl_client *client,
			  struct wl_resource *resource,
			  const char *title)
{
	struct tw_xdg_surface *xdg_surf =
		xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_title(&xdg_surf->base, title);
}

static void
handle_toplevel_set_app_id(struct wl_client *client,
			   struct wl_resource *resource,
			   const char *app_id)
{
	struct tw_xdg_surface *xdg_surf =
		xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_class(&xdg_surf->base, app_id);
}

/* desktop.show_window_menu */
static void
handle_toplevel_show_window_menu(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *seat,
				 uint32_t serial,
				 int32_t x,
				 int32_t y)
{
	//TODO verify the serial
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	desktop->api.show_window_menu(&xdg_surf->base, seat, x, y,
	                              desktop->user_data);
}

/* desktop.move */
static void
handle_toplevel_move(struct wl_client *client,
		     struct wl_resource *resource,
		     struct wl_resource *seat,
		     uint32_t serial)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	tw_desktop_surface_move(&xdg_surf->base, seat, serial);
}

/* desktop.resize */
static void
handle_toplevel_resize(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *seat,
		       uint32_t serial,
		       uint32_t edges)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	if (edges > XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT) {
		tw_logl("xdg resize requested on invalid edge");
		return;
	}
	tw_desktop_surface_resize(&xdg_surf->base, seat, edges, serial);
}

static void
handle_toplevel_set_max_size(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t width,
			     int32_t height)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	xdg_surf->toplevel.pending.max_size.w = width;
	xdg_surf->toplevel.pending.max_size.h = height;
}

static void
handle_toplevel_set_min_size(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t width,
			     int32_t height)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	xdg_surf->toplevel.pending.min_size.w = width;
	xdg_surf->toplevel.pending.max_size.h = height;
}

/* desktop.maximized_requested */
static void
handle_toplevel_set_maximized(struct wl_client *client,
			      struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_maximized(&xdg_surf->base, true);
}

/* desktop.maximized_requested */
static void
handle_toplevel_unset_maximized(struct wl_client *client,
                                struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_maximized(&xdg_surf->base, false);
}

/* desktop.fullscreen_request */
static void
handle_toplevel_set_fullscreen(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *output)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_fullscreen(&xdg_surf->base, output, true);
}

/* desktop.fullscreen_request */
static void
handle_toplevel_unset_fullscreen(struct wl_client *client,
                                 struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	tw_desktop_surface_set_fullscreen(&xdg_surf->base, NULL, false);
}

/* desktop.minimized_request */
static void
handle_toplevel_minimize(struct wl_client *client, struct wl_resource *res)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(res);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;

	desktop->api.minimized_requested(&xdg_surf->base, desktop->user_data);
}

static const struct xdg_toplevel_interface toplevel_impl = {
	.destroy = handle_toplevel_destroy,
	.set_parent = handle_toplevel_set_parent,
	.set_title = handle_toplevel_set_title,
	.set_app_id = handle_toplevel_set_app_id,
	.show_window_menu = handle_toplevel_show_window_menu,
	.move = handle_toplevel_move,
	.resize = handle_toplevel_resize,
	.set_max_size = handle_toplevel_set_max_size,
	.set_min_size = handle_toplevel_set_min_size,
	.set_maximized = handle_toplevel_set_maximized,
	.unset_maximized = handle_toplevel_unset_maximized,
	.set_fullscreen = handle_toplevel_set_fullscreen,
	.unset_fullscreen = handle_toplevel_unset_fullscreen,
	.set_minimized = handle_toplevel_minimize,
};

/* desktop.surface_removed */
static void
destroy_toplevel_resource(struct wl_resource *resource)
{
	struct tw_xdg_surface *surf =
		xdg_surface_from_toplevel(resource);
	if (!surf)
		return;
	tw_desktop_surface_rm(&surf->base);
	surf->toplevel.resource = NULL;
}

/* desktop.surface_added */
static void
handle_get_toplevel(struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t id)
{
	uint32_t version = wl_resource_get_version(resource);
	struct tw_desktop_surface *surf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_xdg_surface *xdg_surf =
		container_of(surf, struct tw_xdg_surface, base);
	struct wl_resource *toplevel_res =
		wl_resource_create(client, &xdg_toplevel_interface,
		                   version, id);
	if (!toplevel_res) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(toplevel_res, &toplevel_impl,
	                               xdg_surf, destroy_toplevel_resource);

	xdg_surf->toplevel.resource = toplevel_res;
	xdg_surf->toplevel.current.max_size.w = UINT32_MAX;
	xdg_surf->toplevel.current.max_size.h = UINT32_MAX;
	xdg_surf->toplevel.current.min_size.w = 0;
	xdg_surf->toplevel.current.min_size.h = 0;
	xdg_surface_set_role(surf, TW_DESKTOP_TOPLEVEL_SURFACE);
	tw_desktop_surface_add(surf);
}

/******************************** xdg popup **********************************/

static const struct xdg_popup_interface popup_impl;

static struct tw_xdg_surface *
xdg_surface_from_popup(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &xdg_popup_interface,
	                               &popup_impl));
	return wl_resource_get_user_data(resource);
}

static void
popup_reposition(struct tw_xdg_surface *surf,
                 struct tw_xdg_positioner *positioner)
{
	//this defines the basic geometry.
	struct tw_xdg_surface *parent = surf->popup.parent;
	pixman_rectangle32_t geometry = {
		.x = positioner->offset.x,
		.y = positioner->offset.y,
		.width = positioner->size.width,
		.height = positioner->size.height,
	};
	//determine the anchor point
	switch (positioner->anchor.anchor) {
	case XDG_POSITIONER_ANCHOR_TOP:
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
	case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
		geometry.y += positioner->anchor.y;
		break;
	case XDG_POSITIONER_ANCHOR_BOTTOM:
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		geometry.y += positioner->anchor.y + positioner->anchor.height;
		break;
	default:
		geometry.y += positioner->anchor.y +
			positioner->anchor.height / 2;
	}
	switch (positioner->anchor.anchor) {
	case XDG_POSITIONER_ANCHOR_LEFT:
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
		geometry.x += positioner->anchor.x;
		break;
	case XDG_POSITIONER_ANCHOR_RIGHT:
	case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		geometry.x += positioner->anchor.x + positioner->anchor.width;
		break;
	default:
		geometry.x += positioner->anchor.x +
			positioner->anchor.width / 2;
	}
	//determine how the popup itself repositioned
        switch (positioner->gravity) {
	case XDG_POSITIONER_GRAVITY_TOP:
	case XDG_POSITIONER_GRAVITY_TOP_LEFT:
	case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
		geometry.y -= geometry.height;
		break;
	case XDG_POSITIONER_GRAVITY_BOTTOM:
	case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
		break;
	default:
		geometry.y -= geometry.height / 2;
	}
	switch (positioner->gravity) {
	case XDG_POSITIONER_GRAVITY_LEFT:
	case XDG_POSITIONER_GRAVITY_TOP_LEFT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
		geometry.x -= geometry.width;
		break;
	case XDG_POSITIONER_GRAVITY_RIGHT:
	case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
		geometry.x = geometry.x;
		break;
	default:
		geometry.x -= geometry.width / 2;
	}

        if (positioner->contraint !=
	    XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE) {
	        //TODO: handle constraint_adjustment
	}
        tw_subsurface_update_pos(&surf->popup.subsurface,
                                 geometry.x+parent->base.window_geometry.x,
                                 geometry.y+parent->base.window_geometry.y);
        surf->base.configure(&surf->base, 0, geometry.x, geometry.y,
                             geometry.width, geometry.height);
}

static void
handle_popup_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
notify_close_popup(struct wl_listener *listener, void *data)
{
	struct tw_xdg_surface *surface =
		container_of(listener, struct tw_xdg_surface,
		             popup.close_popup_listener);
	surface->base.close(&surface->base);
}

static void
start_popup_grab_idle(void *data)
{
	struct tw_popup_grab *grab = data;

	tw_popup_grab_start(grab);
}

static void
handle_popup_grab(struct wl_client *client,
                  struct wl_resource *resource,
                  struct wl_resource *seat,
                  uint32_t serial)
{
	struct tw_xdg_surface *xdg_surface =
		xdg_surface_from_popup(resource);

	struct tw_seat *tw_seat = tw_seat_from_resource(seat);
	struct wl_event_loop *loop =
		wl_display_get_event_loop(tw_seat->display);
	struct tw_popup_grab *grab =
		tw_popup_grab_create(xdg_surface->base.tw_surface,
		                     xdg_surface->popup.resource,
		                     tw_seat);
	if (!grab) {
		wl_resource_post_no_memory(xdg_surface->popup.resource);
		return;
	}
	tw_signal_setup_listener(&grab->close,
	                         &xdg_surface->popup.close_popup_listener,
	                         notify_close_popup);
	wl_event_loop_add_idle(loop, start_popup_grab_idle, grab);
}

static void
handle_popup_reposition(struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *positioner_res,
                        uint32_t token)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_popup(resource);
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(positioner_res);
	popup_reposition(xdg_surf, positioner);
}

static const struct xdg_popup_interface popup_impl = {
	.destroy = handle_popup_destroy,
	.grab = handle_popup_grab,
	.reposition = handle_popup_reposition,
};

static void
destroy_popup_resource(struct wl_resource *resource)
{
	struct tw_xdg_surface *surf =
		wl_resource_get_user_data(resource);
	if (!surf)
		return;
	tw_reset_wl_list(&surf->popup.subsurface.parent_link);
	surf->popup.resource = NULL;
}

/******************************************************************************
 * xdg_surface interface
 *****************************************************************************/

static void
handle_destroy_xdg_surface(struct wl_client *client,
                           struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
popup_init(struct tw_xdg_surface *popup, struct wl_resource *popup_resource,
           struct tw_xdg_surface *parent)
{
	struct tw_subsurface *subsurface = &popup->popup.subsurface;
	popup->popup.resource = popup_resource;
	popup->popup.parent = parent;
	xdg_surface_set_role(&popup->base, TW_DESKTOP_POPUP_SURFACE);

	subsurface->parent = parent->base.tw_surface;
	subsurface->surface = popup->base.tw_surface;
	subsurface->sync = false;
	wl_list_init(&subsurface->parent_link);
	wl_list_insert(subsurface->parent->subsurfaces.prev,
	               &subsurface->parent_link);
}

static void
handle_get_popup(struct wl_client *client,
                 struct wl_resource *resource,
                 uint32_t id,
                 struct wl_resource *parent_resource,
                 struct wl_resource *positioner_resource)
{
	struct wl_resource *r;
	uint32_t version = wl_resource_get_version(resource);
	struct tw_desktop_surface *dsurf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_desktop_surface *parent_dsurf =
		desktop_surface_from_xdg_surface(parent_resource);
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);
	struct tw_xdg_surface *parent_xdg_surf =
		container_of(parent_dsurf, struct tw_xdg_surface, base);
	struct tw_xdg_positioner *tw_positioner =
		positioner_from_resource(positioner_resource);

	r = wl_resource_create(client, &xdg_popup_interface, version, id);
	if (!r) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(r, &popup_impl, xdg_surf,
	                               destroy_popup_resource);
	popup_init(xdg_surf, r, parent_xdg_surf);
	popup_reposition(xdg_surf, tw_positioner);
}

static void
handle_set_window_geometry(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct tw_desktop_surface *dsurf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_xdg_surface *surf =
		container_of(dsurf, struct tw_xdg_surface, base);
	if (!is_tw_surface_xdg_surface(dsurf->tw_surface)) {
		wl_resource_post_error(resource,
		                       XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
		                       "xdg_surface must have a role");
		return;
	}
	if (width < 1 || height < 1) {
		wl_resource_post_error(resource, -1,
		                       "invalid window geometry");
		return;
	}
	surf->next_window_geometry.x1 = x;
	surf->next_window_geometry.y1 = y;
	surf->next_window_geometry.x2 = x+width;
	surf->next_window_geometry.y2 = y+height;
	surf->has_next_window_geometry = true;
}

static void
handle_ack_configure(struct wl_client *client,
                     struct wl_resource *resource, uint32_t serial)
{
	struct tw_xdg_surface *xdg_surf =
		container_of(desktop_surface_from_xdg_surface(resource),
		             struct tw_xdg_surface, base);
	struct tw_surface *surface = xdg_surf->base.tw_surface;

	if (!tw_surface_has_role(surface)) {
		wl_resource_post_error(resource,
		                       XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
		                       "xdg_surface does not have a role");
		return;
	}
	xdg_surf->configured = true;
}

static const struct xdg_surface_interface xdg_surface_impl = {
	.destroy = handle_destroy_xdg_surface,
	.get_toplevel = handle_get_toplevel,
	.get_popup = handle_get_popup,
	.set_window_geometry = handle_set_window_geometry,
	.ack_configure = handle_ack_configure,
};

static void
destroy_xdg_surface_resource(struct wl_resource *resource)
{
	struct tw_desktop_surface *dsurf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);
        //handle role
        if (dsurf->type == TW_DESKTOP_TOPLEVEL_SURFACE &&
	    xdg_surf->toplevel.resource) {
	        destroy_toplevel_resource(xdg_surf->toplevel.resource);
		wl_resource_set_user_data(xdg_surf->toplevel.resource, NULL);
        } else if (dsurf->type == TW_DESKTOP_POPUP_SURFACE &&
                   xdg_surf->popup.resource) {
	        destroy_popup_resource(xdg_surf->popup.resource);
		wl_resource_set_user_data(xdg_surf->popup.resource, NULL);
        }
        if (dsurf->tw_surface)
	        tw_reset_wl_list(&xdg_surf->surface_destroy.link);
	tw_desktop_surface_fini(dsurf);
	free(xdg_surf);
}

/******************************************************************************
 * xdg_positioner interface
 *****************************************************************************/
static const struct xdg_positioner_interface xdg_positioner_impl;

static struct tw_xdg_positioner *
positioner_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &xdg_positioner_interface,
	                               &xdg_positioner_impl));
	return wl_resource_get_user_data(resource);
}

static void
handle_positioner_destroy(struct wl_client *client,
                          struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_positioner_set_size(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t width, int32_t height)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	if (width <= 0 || height <= 0) {
		wl_resource_post_error(resource,
		                       XDG_POSITIONER_ERROR_INVALID_INPUT,
		                       "invalid set_size (width:%d, height%d)",
		                       width, height);
		return;
	}
	positioner->size.width  = width;
	positioner->size.height = height;
}

static void
handle_positioner_set_anchor_rect(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x, int32_t y,
                                  int32_t width, int32_t height)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	if (width <= 0 || height <= 0) {
		wl_resource_post_error(resource,
		                       XDG_POSITIONER_ERROR_INVALID_INPUT,
		                       "invalid set_anchor_rect "
		                       "(width:%d, height%d)",
		                       width, height);
		return;
	}
	positioner->anchor.x = x;
	positioner->anchor.y = y;
	positioner->anchor.width = width;
	positioner->anchor.height = height;
}

static void
handle_positioner_set_anchor(struct wl_client *client,
                             struct wl_resource *resource, uint32_t anchor)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	if (anchor > XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
		                       XDG_POSITIONER_ERROR_INVALID_INPUT,
		                       "invalid set_anchor %d", anchor);
		return;
	}
	positioner->anchor.anchor = anchor;
}

static void
handle_positioner_set_gravity(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t gravity)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	if (gravity > XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
		                       XDG_POSITIONER_ERROR_INVALID_INPUT,
		                       "invalid set_gravity %d", gravity);
		return;
	}
	positioner->gravity = gravity;
}

static void
handle_positioner_constraint_adjustment(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t constraint)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	if (constraint & (~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y)) {
		wl_resource_post_error(resource,
		                       XDG_POSITIONER_ERROR_INVALID_INPUT,
		                       "invalid set_contraint_adjustment %d",
		                       constraint);
		return;
	}
	positioner->contraint = constraint;
}

static void
handle_positioner_set_offset(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	positioner->offset.x = x;
	positioner->offset.y = y;
}

static void
handle_positioner_set_reactive(struct wl_client *client,
                               struct wl_resource *resource)
{}

static void
handle_positioner_set_parent_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t parent_width, int32_t parent_height)
{}

static void
handle_positioner_set_parent_configure(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t serial)
{}

static const struct xdg_positioner_interface xdg_positioner_impl = {
	.destroy = handle_positioner_destroy,
	.set_size = handle_positioner_set_size,
	.set_anchor_rect = handle_positioner_set_anchor_rect,
	.set_anchor = handle_positioner_set_anchor,
	.set_gravity = handle_positioner_set_gravity,
	.set_constraint_adjustment = handle_positioner_constraint_adjustment,
	.set_reactive = handle_positioner_set_reactive,
	.set_offset = handle_positioner_set_offset,
	.set_parent_size = handle_positioner_set_parent_size,
	.set_parent_configure = handle_positioner_set_parent_configure,
};

static void
destroy_positioner_res(struct wl_resource *resource)
{
	struct tw_xdg_positioner *positioner =
		positioner_from_resource(resource);
	free(positioner);
}

/******************************************************************************
 * xdg_wm_base interface
 *****************************************************************************/

static void
handle_destroy_wm_base(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_create_positioner(struct wl_client *client,
                         struct wl_resource *wm_base_resource, uint32_t id)
{
	uint32_t ver = wl_resource_get_version(wm_base_resource);
	struct tw_xdg_positioner *positioner;
	struct wl_resource *resource = NULL;

	if (!tw_create_wl_resource_for_obj(resource, positioner, client, id,
	                                   ver, xdg_positioner_interface)) {
		wl_resource_post_no_memory(wm_base_resource);
		return;
	}
	positioner->resource = resource;
	wl_resource_set_implementation(resource, &xdg_positioner_impl,
	                               positioner, destroy_positioner_res);
}

static void
handle_create_xdg_surface(struct wl_client *client,
                          struct wl_resource *resource, uint32_t id,
                          struct wl_resource *surface)
{
	//okay, now xdg_surface is not a role
	struct wl_resource *r = NULL;
	struct tw_xdg_surface *dsurf = NULL;
	uint32_t version = wl_resource_get_version(resource);
	struct tw_desktop_manager *desktop =
		wl_resource_get_user_data(resource);

	if (!tw_create_wl_resource_for_obj(r, dsurf, client, id, version,
	                                   xdg_surface_interface)) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(r, &xdg_surface_impl, &dsurf->base,
	                               destroy_xdg_surface_resource);
	init_xdg_surface(dsurf, surface, r, desktop);
}

static void
handle_pong(struct wl_client *client, struct wl_resource *resource,
            uint32_t serial)
{
	//TODO how to I handle pong, I dont even send out ping.
}

static struct xdg_wm_base_interface xdg_wm_base_impl = {
	.destroy = handle_destroy_wm_base,
	.create_positioner = handle_create_positioner,
	.get_xdg_surface = handle_create_xdg_surface,
	.pong = handle_pong,
};

static void
destroy_wm_base(struct wl_resource *r)
{

}


static void
bind_xdg_wm_base(struct wl_client *wl_client, void *data,
                 uint32_t version, uint32_t id)
{
	struct wl_resource *r = NULL;

	r = wl_resource_create(wl_client, &xdg_wm_base_interface, version, id);
	if (!r) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(r, &xdg_wm_base_impl, data,
	                               destroy_wm_base);
}

bool
init_xdg_shell(struct tw_desktop_manager *desktop)
{
	desktop->xdg_shell_global =
		wl_global_create(desktop->display, &xdg_wm_base_interface,
		                 XDG_SHELL_VERSION, desktop,
		                 bind_xdg_wm_base);
	if (!desktop->xdg_shell_global)
		return false;
	return true;
}
