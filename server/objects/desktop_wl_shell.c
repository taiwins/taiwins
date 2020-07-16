/*
 * destkop.c - taiwins wl_shell_surface implementation
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
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <wayland-util.h>

#include "objects/logger.h"
#include "utils.h"
#include "surface.h"
#include "seat.h"
#include "desktop.h"
#include "popup_grab.h"

#define WL_SHELL_VERSION 1

struct tw_wl_shell_surface {
	struct tw_desktop_surface base;
	/* used only by popup */
	struct wl_listener popup_close;
	struct wl_listener surface_destroy;
};


static const char *TW_DESKTOP_TOPLEVEL_WL_SHELL_NAME = "wl_shell_toplevel";
static const char *TW_DESKTOP_POPUP_WL_SHELL_NAME = "wl_shell_popup";
static const char *TW_DESKTOP_TRANSIENT_WL_SHELL_NAME = "wl_shell_transient";

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
	dsurf->type = type;

	if (type == TW_DESKTOP_TOPLEVEL_SURFACE)
		tw_surface->role.name = TW_DESKTOP_TOPLEVEL_WL_SHELL_NAME;
	else if (type == TW_DESKTOP_TRANSIENT_SURFACE)
		tw_surface->role.name = TW_DESKTOP_TRANSIENT_WL_SHELL_NAME;
	else
		tw_surface->role.name = TW_DESKTOP_POPUP_WL_SHELL_NAME;
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
	struct tw_seat *tw_seat = tw_seat_from_resource(seat);

	if (!tw_seat_valid_serial(tw_seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}
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
	struct tw_seat *tw_seat = tw_seat_from_resource(seat);

	if (!tw_seat_valid_serial(tw_seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}

	d->desktop->api.resize(d, seat, serial, edges, d->desktop->user_data);

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
	d->desktop->api.maximized_requested(d, !d->maximized,
	                                    d->desktop->user_data);
	d->maximized = !d->maximized;
}

static void
handle_set_toplevel(struct wl_client *client,
                    struct wl_resource *resource)
{
	struct tw_desktop_surface *d =
		tw_desktop_surface_from_wl_shell_surface(resource);

	tw_desktop_surface_set_role(d, TW_DESKTOP_TOPLEVEL_SURFACE);
	tw_desktop_surface_add(d);
}

/************************* transient_surface *********************************/

static void
handle_transient_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_subsurface *subsurface =
		container_of(listener, struct tw_subsurface,
		             surface_destroyed);
	wl_list_remove(&subsurface->parent_link);
	free(subsurface);
}

static void
transient_impl_subsurface(struct tw_subsurface *subsurface,
                          struct tw_desktop_surface *dsurf,
                          struct wl_resource *parent, int32_t x, int32_t y)
{
	subsurface->parent = tw_surface_from_resource(parent);
	subsurface->surface = tw_surface_from_resource(dsurf->wl_surface);
	subsurface->sx = x;
	subsurface->sy = y;
	subsurface->sync = false;

	wl_list_init(&subsurface->parent_link);
	wl_list_insert(subsurface->parent->subsurfaces.prev,
	               &subsurface->parent_link);
	tw_set_resource_destroy_listener(dsurf->wl_surface,
	                                 &subsurface->surface_destroyed,
	                                 handle_transient_surface_destroy);
}

static void
handle_set_transient(struct wl_client *client,
                     struct wl_resource *resource, struct wl_resource *parent,
                     int32_t x, int32_t y, uint32_t flags)
{
	struct tw_subsurface *subsurface;
	struct tw_desktop_surface *dsurf =
		tw_desktop_surface_from_wl_shell_surface(resource);

	subsurface = calloc(1, sizeof(*subsurface));
	if (!subsurface) {
		wl_resource_post_no_memory(resource);
		return;
	}
	transient_impl_subsurface(subsurface, dsurf, parent, x, y);
	tw_desktop_surface_set_role(dsurf, TW_DESKTOP_TRANSIENT_SURFACE);
}

/******************************** popup **************************************/

static void
close_wl_shell_popup(struct wl_listener *listener, void *data)
{
	struct tw_wl_shell_surface *surface =
		container_of(listener, struct tw_wl_shell_surface,
		             popup_close);
	wl_shell_surface_send_popup_done(surface->base.shell_surface);
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
	struct tw_desktop_surface *popup =
		tw_desktop_surface_from_wl_shell_surface(resource);
	struct tw_wl_shell_surface *shell_surf =
		container_of(popup, struct tw_wl_shell_surface, base);
	struct tw_surface *tw_surface =
		tw_surface_from_resource(popup->wl_surface);
	struct tw_subsurface *subsurface;
	struct tw_popup_grab *grab;

	subsurface = calloc(1, sizeof(*subsurface));
	if (!subsurface) {
		wl_resource_post_no_memory(resource);
		return;
	}
	grab = tw_popup_grab_create(tw_surface, popup->shell_surface);
	if (!grab) {
		wl_resource_post_no_memory(resource);
		free(subsurface);
		return;
	}

	transient_impl_subsurface(subsurface, popup, parent, x, y);
	tw_signal_setup_listener(&grab->close, &shell_surf->popup_close,
	                         close_wl_shell_popup);
	tw_desktop_surface_set_role(popup, TW_DESKTOP_POPUP_SURFACE);
}

static void
handle_set_title(struct wl_client *client, struct wl_resource *resource,
                 const char *title)
{
	struct tw_desktop_surface *dsurf =
		tw_desktop_surface_from_wl_shell_surface(resource);
	char *tmp = strdup(title);
	if (!tmp)
		return;
	free(dsurf->title);
	dsurf->title = tmp;
}

static void
handle_set_class(struct wl_client *client, struct wl_resource *resource,
                 const char *class)
{
	struct tw_desktop_surface *dsurf =
		tw_desktop_surface_from_wl_shell_surface(resource);
	char *tmp = strdup(class);
	if (!tmp)
		return;
	free(dsurf->class);
	dsurf->class = tmp;
}

static const struct wl_shell_surface_interface wl_shell_surf_impl = {
	.pong = handle_surface_pong,
	.move = handle_surface_move,
	.resize = handle_surface_resize,
	.set_toplevel = handle_set_toplevel,
	.set_transient = handle_set_transient,
	.set_fullscreen = handle_set_fullscreen,
	.set_popup = handle_set_popup,
	.set_maximized = handle_set_maximized,
	.set_title = handle_set_title,
	.set_class = handle_set_class,
};

static void
destroy_wl_shell_surf_resource(struct wl_resource *resource)
{
	struct tw_desktop_surface *dsurf =
		tw_desktop_surface_from_wl_shell_surface(resource);
	struct tw_wl_shell_surface *surf =
		container_of(dsurf, struct tw_wl_shell_surface, base);

	tw_desktop_surface_rm(dsurf);
	tw_reset_wl_list(&surf->surface_destroy.link);
	tw_desktop_surface_fini(dsurf);

	free(surf);
}

static void
handle_shell_surface_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_wl_shell_surface *surf =
		container_of(listener, struct tw_wl_shell_surface,
		             surface_destroy);

        tw_desktop_surface_rm(&surf->base);
	surf->base.wl_surface = NULL;
}

static void
handle_shell_surface_configure(struct tw_desktop_surface *surface,
                               enum wl_shell_surface_resize edge,
                               int32_t x, int32_t y,
                               unsigned width, unsigned height)
{
	wl_shell_surface_send_configure(surface->shell_surface, edge,
	                                width, height);
}

static void
handle_get_wl_shell_surface(struct wl_client *client,
                            struct wl_resource *shell,
                            uint32_t id,
                            struct wl_resource *wl_surface)
{
	struct tw_wl_shell_surface *surf;
	struct tw_desktop_surface *dsurf;
	struct wl_resource *resource = NULL;
	uint32_t version = wl_resource_get_version(shell);
	struct tw_desktop_manager *desktop = wl_resource_get_user_data(shell);
	struct tw_surface *surface = tw_surface_from_resource(wl_surface);
	uint32_t surf_id = wl_resource_get_id(wl_surface);

	if (surface->role.commit &&
	    surface->role.commit != tw_desktop_surface_commit) {
		wl_resource_post_error(wl_surface, WL_SHELL_ERROR_ROLE,
		                       "wl_surface@%d already has "
		                       "another role", surf_id);
		return;
	}
	if (!tw_create_wl_resource_for_obj(resource, surf, client, id, version,
	                                   wl_shell_surface_interface)) {
		wl_resource_post_no_memory(shell);
		return;
	}

	dsurf = &surf->base;
	dsurf->configure = handle_shell_surface_configure;

	wl_resource_set_implementation(resource, &wl_shell_surf_impl,
	                               dsurf, destroy_wl_shell_surf_resource);
	tw_desktop_surface_init(dsurf, wl_surface, resource, desktop);

	tw_set_resource_destroy_listener(wl_surface, &surf->surface_destroy,
	                                 handle_shell_surface_surface_destroy);
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

bool
init_wl_shell(struct tw_desktop_manager *desktop)
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
