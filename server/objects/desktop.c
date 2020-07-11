/*
 * destkop.c - taiwins wayland destkop protocols
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
#include <stdlib.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wayland-xdg-shell-server-protocol.h>

#include "ctypes/helpers.h"
#include "desktop.h"
#include "surface.h"

#define WL_SHELL_VERSION 1
#define TW_DESKTOP_TOPLEVEL_SURFACE_NAME "toplevel_surface"
#define TW_DESKTOP_POPUP_SURFACE_NAME "popup_surface"

enum tw_desktop_surface_type {
	TW_DESKTOP_TOPLEVEL_SURFACE,
	TW_DESKTOP_POPUP_SURFACE,
};

struct tw_desktop_surface {
	struct wl_resource *shell_surface; /**< shared by implementation */
	struct wl_resource *wl_surface;

	struct tw_desktop *desktop;
	bool fullscreened;
	bool maximized;
};

static void
tw_desktop_surface_commit(struct tw_surface *surface)
{
	//Opps, nothing
}

static void
tw_desktop_surface_set_role(struct tw_desktop_surface *dsurf,
                            enum tw_desktop_surface_type type)
{
	struct tw_surface *tw_surface =
		tw_surface_from_resource(dsurf->wl_surface);
	tw_surface->role.commit = tw_desktop_surface_commit;
	tw_surface->role.commit_private = dsurf;
	if (type == TW_DESKTOP_TOPLEVEL_SURFACE)
		tw_surface->role.name = TW_DESKTOP_TOPLEVEL_SURFACE_NAME;
	else
		tw_surface->role.name = TW_DESKTOP_POPUP_SURFACE_NAME;
}

static const struct wl_shell_surface_interface wl_shell_surf_impl;

static struct tw_desktop_surface *
tw_desktop_surface_from_wl_shell_surface(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wl_shell_surface_interface,
	                               &wl_shell_surf_impl));
	return wl_resource_get_user_data(resource);

}

static void
handle_surface_pong(struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t serial)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);
	d->desktop->api.pong(d, d->desktop->user_data);
}

static void
handle_surface_move(struct wl_client *client,
                    struct wl_resource *resource,
                    struct wl_resource *seat,
                    uint32_t serial)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);
	d->desktop->api.move(d, seat, serial, d->desktop->user_data);
}

static void
handle_surface_resize(struct wl_client *client,
                      struct wl_resource *resource,
                      struct wl_resource *seat,
                      uint32_t serial,
                      uint32_t edges)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);
	d->desktop->api.resize(d, seat, serial, edges, d->desktop->user_data);

}

static void
handle_set_toplevel(struct wl_client *client,
                    struct wl_resource *resource)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);
	struct tw_surface *surface =
		tw_surface_from_resource(d->wl_surface);
	uint32_t id = wl_resource_get_id(d->wl_surface);
	if (surface->role.commit &&
	    surface->role.commit != tw_desktop_surface_commit) {
		wl_resource_post_error(resource, WL_SHELL_ERROR_ROLE,
		                       "wl_surface@%u already has a role", id);
		return;
	}
	tw_desktop_surface_set_role(d, TW_DESKTOP_TOPLEVEL_SURFACE);
	d->desktop->api.surface_added(d, d->desktop->user_data);
}

static void
handle_set_fullscreen(struct wl_client *client,
                      struct wl_resource *resource,
                      uint32_t method,
                      uint32_t framerate,
                      struct wl_resource *output)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);

	d->desktop->api.fullscreen_requested(d, output, !d->fullscreened,
	                                     d->desktop->user_data);
	d->fullscreened = !d->fullscreened;
}

static void
handle_set_maximized(struct wl_client *client,
                     struct wl_resource *resource,
                     struct wl_resource *output)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);
	d->desktop->api.maximized_requested(d, output,
	                                    !d->maximized,
	                                    d->desktop->user_data);
	d->maximized = !d->maximized;
}

static void
handle_set_popup(struct wl_client *client,
                 struct wl_resource *resource,
                 struct wl_resource *seat,
                 uint32_t serial,
                 struct wl_resource *parent,
                 int32_t x,
                 int32_t y,
                 uint32_t flags)
{
	//enter the popup in a custom
}

static const struct wl_shell_surface_interface wl_shell_surf_impl = {
	.pong = handle_surface_pong,
	.move = handle_surface_move,
	.resize = handle_surface_resize,
	.set_toplevel = handle_set_toplevel,
	.set_transient = NULL,
	.set_fullscreen = handle_set_fullscreen,
	.set_popup = handle_set_popup,
	.set_maximized = handle_set_maximized,
	.set_title = NULL,
	.set_class = NULL,
};



static void
destroy_wl_shell_surf_resource(struct wl_resource *resource)
{
	struct tw_desktop_surface *surf =
		tw_desktop_surface_from_wl_shell_surface(resource);

	free(surf);
}

static void
handle_get_wl_shell_surface(struct wl_client *client,
                            struct wl_resource *shell,
                            uint32_t id,
                            struct wl_resource *surface)
{
	struct wl_resource *resource;
	uint32_t version = wl_resource_get_version(shell);
	struct tw_desktop *desktop = wl_resource_get_user_data(shell);

	struct tw_desktop_surface *surf =
		calloc(1, sizeof(struct tw_desktop_surface));
	if (!surf) {
		wl_resource_post_no_memory(shell);
		return;
	}
	resource = wl_resource_create(client,
	                              &wl_shell_surface_interface,
	                              version, id);
	if (!resource) {
		wl_resource_post_no_memory(shell);
		free(surf);
		return;
	}
	wl_resource_set_implementation(resource, &wl_shell_surf_impl,
	                               surf, destroy_wl_shell_surf_resource);
	surf->shell_surface = resource;
	surf->desktop = desktop;

	if (surf->desktop->api.surface_added)
		surf->desktop->api.surface_added(surf, desktop->user_data);
}

static const struct wl_shell_interface wl_shell_impl = {
	.get_shell_surface = handle_get_wl_shell_surface,
};

static void
destroy_wl_shell_resource(struct wl_resource *r)
{
}

static void
bind_wl_shell(struct wl_client *wl_client, void *data,
              uint32_t version, uint32_t id)
{
	struct wl_resource *r = NULL;

	r = wl_resource_create(wl_client, &wl_shell_interface, version, id);
	if (!r) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(r, &wl_shell_impl, data,
	                               destroy_wl_shell_resource);

}

static bool
init_wl_shell(struct tw_desktop *desktop)
{
	desktop->wl_shell_global =
		wl_global_create(desktop->display,
		                 &wl_shell_interface,
		                 WL_SHELL_VERSION, desktop,
		                 bind_wl_shell);
	if (!desktop->wl_shell_global)
		return false;
	return true;
}

static void
tw_desktop_fini(struct tw_desktop *desktop)
{
	if (desktop->wl_shell_global)
		wl_global_destroy(desktop->wl_shell_global);
	desktop->wl_shell_global = NULL;
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_desktop *desktop =
		container_of(listener, struct tw_desktop, destroy_listener);
	tw_desktop_fini(desktop);
}

bool
tw_desktop_init(struct tw_desktop *desktop,
                struct wl_display *display,
                const struct tw_desktop_surface_api *api,
                void *user_data,
                enum tw_desktop_init_option option)
{
	bool ret = true;
	if (!option)
		return false;

	desktop->display = display;
	desktop->user_data = user_data;

	switch (option) {
	case TW_DESKTOP_INIT_INCLUDE_WL_SHELL:
		ret = ret && init_wl_shell(desktop);
	case TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL:
		break;
	case TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_V6:
		break;
	}
	if (!ret)
		tw_desktop_fini(desktop);

	wl_list_init(&desktop->destroy_listener.link);
	desktop->destroy_listener.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &desktop->destroy_listener);

	return ret;
}
