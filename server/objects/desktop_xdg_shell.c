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
#include <wayland-util.h>
#include <wayland-xdg-shell-server-protocol.h>
#include <ctypes/helpers.h>

#include "utils.h"
#include "desktop.h"
#include "logger.h"
#include "surface.h"

#define XDG_SHELL_VERSION 2

struct xdg_size {
	uint32_t w, h;
};

struct tw_xdg_surface {
	struct tw_desktop_surface base;
	/** used for toplevel.set_parent or popup surface */
	struct tw_subsurface subsurface;
	struct wl_resource *fullscreen_output;
	struct {
		struct xdg_size min_size, max_size;
	} pending;

	struct {
		struct xdg_size min_size, max_size;
	} current;
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



static struct tw_desktop_surface *
desktop_surface_from_xdg_surface(struct wl_resource *wl_resource)
{
	assert(wl_resource_instance_of(wl_resource, &xdg_surface_interface,
	                               &xdg_surface_impl));
	return wl_resource_get_user_data(wl_resource);
}

static void
commit_xdg_toplevel(struct tw_surface *surface)
{
	struct tw_desktop_surface *dsurf = surface->role.commit_private;
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);
	struct tw_desktop_manager *desktop = dsurf->desktop;

	xdg_surf->current.max_size = xdg_surf->pending.max_size;
	xdg_surf->current.min_size = xdg_surf->pending.min_size;

	desktop->api.committed(dsurf, desktop->user_data);
}

static void
commit_xdg_popup(struct tw_surface *surface)
{
	//set position!
}

static bool
xdg_surface_set_role(struct tw_desktop_surface *dsurf,
                     enum tw_desktop_surface_type type)
{
	struct tw_surface *surface =
		tw_surface_from_resource(dsurf->wl_surface);
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
	return true;
}

static void
handle_destroy_xdg_surface(struct wl_client *client,
                           struct wl_resource *resource)
{
	wl_resource_destroy(resource);
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
			   struct wl_resource *parent)
{
	struct tw_xdg_surface *xdg_surf =
		xdg_surface_from_toplevel(resource);
	struct tw_xdg_surface *xdg_parent =
		xdg_surface_from_toplevel(parent);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;

	desktop->api.set_parent(&xdg_surf->base, &xdg_parent->base,
	                        desktop->user_data);

}

static void
handle_toplevel_set_title(struct wl_client *client,
			  struct wl_resource *resource,
			  const char *title)
{
	struct tw_xdg_surface *xdg_surf =
		xdg_surface_from_toplevel(resource);
	char *tmp = strdup(title);
	if (!tmp)
		return;
	free(xdg_surf->base.title);
	xdg_surf->base.title = tmp;
}

static void
handle_toplevel_set_app_id(struct wl_client *client,
			   struct wl_resource *resource,
			   const char *app_id)
{
	struct tw_xdg_surface *xdg_surf =
		xdg_surface_from_toplevel(resource);
	char *tmp = strdup(app_id);
	if (!tmp)
		return;
	free(xdg_surf->base.class);
	xdg_surf->base.class = tmp;
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
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	desktop->api.move(&xdg_surf->base, seat, serial, desktop->user_data);
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
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	if (edges > XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT) {
		tw_logl("xdg resize requested on invalid edge");
		return;
	}
	enum wl_shell_surface_resize e = edges;
	desktop->api.resize(&xdg_surf->base, seat, serial, e,
	                    desktop->user_data);
}

static void
handle_toplevel_set_max_size(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t width,
			     int32_t height)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	xdg_surf->pending.max_size.w = width;
	xdg_surf->pending.max_size.h = height;
}

static void
handle_toplevel_set_min_size(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t width,
			     int32_t height)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	xdg_surf->pending.min_size.w = width;
	xdg_surf->pending.max_size.h = height;
}

/* desktop.maximized_requested */
static void
handle_toplevel_set_maximized(struct wl_client *client,
			      struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	if (xdg_surf->base.maximized)
		return;
	desktop->api.maximized_requested(&xdg_surf->base, true,
	                                 desktop->user_data);
	xdg_surf->base.maximized = true;
}

/* desktop.maximized_requested */
static void
handle_toplevel_unset_maximized(struct wl_client *client,
                                struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	if (!xdg_surf->base.maximized)
		return;
	desktop->api.maximized_requested(&xdg_surf->base, false,
	                                 desktop->user_data);
	xdg_surf->base.maximized = false;
}

/* desktop.fullscreen_request */
static void
handle_toplevel_set_fullscreen(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *output)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	if (xdg_surf->base.fullscreened)
		return;
	desktop->api.fullscreen_requested(&xdg_surf->base, output,
	                                  true, desktop->user_data);
	xdg_surf->fullscreen_output = output;
	xdg_surf->base.fullscreened = true;
}

/* desktop.fullscreen_request */
static void
handle_toplevel_unset_fullscreen(struct wl_client *client,
                                 struct wl_resource *resource)
{
	struct tw_xdg_surface *xdg_surf = xdg_surface_from_toplevel(resource);
	struct tw_desktop_manager *desktop = xdg_surf->base.desktop;
	if (!xdg_surf->base.fullscreened)
		return;
	desktop->api.fullscreen_requested(&xdg_surf->base,
	                                  xdg_surf->fullscreen_output, false,
	                                  desktop->user_data);
	xdg_surf->fullscreen_output = NULL;
	xdg_surf->base.fullscreened = false;
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
	struct tw_desktop_surface *surf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_desktop_manager *desktop = surf->desktop;
	desktop->api.surface_removed(surf, desktop->user_data);
}

/* desktop.surface_added */
static void
handle_get_toplevel(struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t id)
{
	struct tw_desktop_surface *surf =
		desktop_surface_from_xdg_surface(resource);
	uint32_t version = wl_resource_get_version(resource);
	struct tw_desktop_manager *desktop = surf->desktop;
	struct wl_resource *toplevel_res =
		wl_resource_create(client, &xdg_toplevel_interface,
		                   version, id);
	if (!toplevel_res) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(toplevel_res, &toplevel_impl, surf,
	                               destroy_toplevel_resource);

	xdg_surface_set_role(surf, TW_DESKTOP_TOPLEVEL_SURFACE);
	desktop->api.surface_added(surf, desktop->user_data);
}


/******************************** xdg popup **********************************/

static const struct xdg_popup_interface popup_impl;


static void
handle_popup_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_popup_grab(struct wl_client *client,
                 struct wl_resource *resource,
                 struct wl_resource *seat,
                 uint32_t serial)
{

}

static void
handle_popup_reposition(struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *positioner,
                        uint32_t token)
{

}

static const struct xdg_popup_interface popup_impl = {
	.destroy = handle_popup_destroy,
	.grab = handle_popup_grab,
	.reposition = handle_popup_reposition,
};

static void
popup_destroy_resource(struct wl_resource *resource)
{

}

static void
handle_get_popup(struct wl_client *client,
                 struct wl_resource *resource,
                 uint32_t id,
                 struct wl_resource *parent,
                 struct wl_resource *positioner)
{
	struct wl_resource *r;
	uint32_t version = wl_resource_get_version(resource);
	struct tw_desktop_surface *dsurf =
		desktop_surface_from_xdg_surface(resource);

	r = wl_resource_create(client, &xdg_popup_interface, version, id);
	if (!r) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(resource, &popup_impl, dsurf,
	                               popup_destroy_resource);
}

static const struct xdg_surface_interface xdg_surface_impl = {
	.destroy = handle_destroy_xdg_surface,
	.get_toplevel = handle_get_toplevel,
	.get_popup = handle_get_popup,
};

static void
destroy_xdg_surface_resource(struct wl_resource *resource)
{
	struct tw_desktop_surface *dsurf =
		desktop_surface_from_xdg_surface(resource);
	struct tw_xdg_surface *xdg_surf =
		container_of(dsurf, struct tw_xdg_surface, base);

	tw_desktop_surface_fini(dsurf);
	free(xdg_surf);
}

struct tw_xdg_positioner {
	struct wl_resource *resource;
	//all kinds of stuff
};

/************************* xdg_wm_base inteface ******************************/

static void
handle_destroy_wm_base(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_create_positioner(struct wl_client *client,
                         struct wl_resource *resource, uint32_t id)
{

}

static void
handle_create_xdg_surface(struct wl_client *client,
                          struct wl_resource *resource, uint32_t id,
                          struct wl_resource *surface)
{
	//okay, now xdg_surface is not a role
	struct wl_resource *r;
	uint32_t version = wl_resource_get_version(resource);
	struct tw_xdg_surface *dsurf =
		calloc(1, sizeof(struct tw_xdg_surface));
	struct tw_desktop_manager *desktop =
		wl_resource_get_user_data(resource);
	if (!dsurf) {
		wl_resource_post_no_memory(resource);
		return;
	}
	r = wl_resource_create(client, &xdg_surface_interface, version, id);
	if (!r) {
		wl_resource_post_no_memory(resource);
		free(dsurf);
		return;
	}
	tw_desktop_surface_init(&dsurf->base, surface, r, desktop);

	dsurf->current.max_size.w = UINT32_MAX;
	dsurf->current.max_size.h = UINT32_MAX;
	dsurf->current.min_size.w = 0;
	dsurf->current.min_size.h = 0;
	wl_resource_set_implementation(r, &xdg_surface_impl, &dsurf->base,
	                               destroy_xdg_surface_resource);

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
