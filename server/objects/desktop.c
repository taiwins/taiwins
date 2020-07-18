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
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>

#include "logger.h"
#include "desktop.h"
#include "surface.h"
#include "seat.h"

bool
init_wl_shell(struct tw_desktop_manager *desktop);

bool
init_xdg_shell(struct tw_desktop_manager *desktop);

static void
tw_desktop_fini(struct tw_desktop_manager *desktop)
{
	if (desktop->wl_shell_global)
		wl_global_destroy(desktop->wl_shell_global);
	desktop->wl_shell_global = NULL;
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_desktop_manager *desktop =
		container_of(listener, struct tw_desktop_manager,
		             destroy_listener);
	tw_desktop_fini(desktop);
}

bool
tw_desktop_init(struct tw_desktop_manager *desktop,
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
	memcpy(&desktop->api, api, sizeof(*api));
	if (option & TW_DESKTOP_INIT_INCLUDE_WL_SHELL)
		ret = ret && init_wl_shell(desktop);
	if (option & TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL)
		ret = ret && init_xdg_shell(desktop);
	if (!ret)
		tw_desktop_fini(desktop);

	wl_list_init(&desktop->destroy_listener.link);
	desktop->destroy_listener.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &desktop->destroy_listener);

	return ret;
}

struct tw_desktop_manager *
tw_desktop_create_global(struct wl_display *display,
                         const struct tw_desktop_surface_api *api,
                         void *user_data,
                         enum tw_desktop_init_option option)
{
	static struct tw_desktop_manager s_desktop_manager = {0};

	if (s_desktop_manager.display)
		return &s_desktop_manager;

	if (!tw_desktop_init(&s_desktop_manager, display, api,
	                     user_data, option))
		return NULL;
	return &s_desktop_manager;
}

void
tw_desktop_surface_init(struct tw_desktop_surface *surf,
                        struct wl_resource *wl_surface,
                        struct wl_resource *resource,
                        struct tw_desktop_manager *desktop)
{
	surf->desktop = desktop;
	surf->tw_surface = tw_surface_from_resource(wl_surface);
	surf->resource = resource;
	surf->fullscreened = false;
	surf->maximized = false;
	surf->surface_added = false;
	surf->title = NULL;
	surf->class = NULL;
}

void
tw_desktop_surface_fini(struct tw_desktop_surface *surf)
{
	if (surf->title)
		free(surf->title);
	if (surf->class)
		free(surf->class);
}

void
tw_desktop_surface_add(struct tw_desktop_surface *surf)
{
	void *user_data = surf->desktop->user_data;

	if (!surf->surface_added) {
		surf->desktop->api.surface_added(surf, user_data);
		surf->surface_added = true;
	}
}

void
tw_desktop_surface_rm(struct tw_desktop_surface *surf)
{
	void *user_data = surf->desktop->user_data;
	if (surf->surface_added && surf->tw_surface) {
		surf->desktop->api.surface_removed(surf, user_data);
		surf->surface_added = false;
	}
}

void
tw_desktop_surface_set_fullscreen(struct tw_desktop_surface *surf,
                                  struct wl_resource *output,
                                  bool fullscreen)
{
	void *user_data = surf->desktop->user_data;
	if (surf->fullscreened != fullscreen)
		surf->desktop->api.fullscreen_requested(surf, output,
		                                        fullscreen, user_data);
}

void
tw_desktop_surface_set_maximized(struct tw_desktop_surface *surf,
                                 bool maximized)
{
	void *user_data = surf->desktop->user_data;
	if (surf->maximized != maximized)
		surf->desktop->api.maximized_requested(surf, maximized,
		                                       user_data);
}

void
tw_desktop_surface_set_title(struct tw_desktop_surface *surf,
                             const char *title)
{
	char *tmp = strdup(title);
	if (!tmp)
		return;
	free(surf->title);
	surf->title = tmp;
}

void
tw_desktop_surface_set_class(struct tw_desktop_surface *surf,
                             const char *class)
{
	char *tmp = strdup(class);
	if (!tmp)
		return;
	free(surf->class);
	surf->class = tmp;
}

void
tw_desktop_surface_move(struct tw_desktop_surface *surf,
                        struct wl_resource *seat, uint32_t serial)
{
	struct tw_seat *tw_seat = tw_seat_from_resource(seat);
	void *user_data = surf->desktop->user_data;

	if (!tw_seat_valid_serial(tw_seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}
	surf->desktop->api.move(surf, seat, serial, user_data);
}

void
tw_desktop_surface_resize(struct tw_desktop_surface *surf,
                          struct wl_resource *seat, uint32_t edge,
                          uint32_t serial)
{
	struct tw_seat *tw_seat = tw_seat_from_resource(seat);
	void *user_data = surf->desktop->user_data;

	if (!tw_seat_valid_serial(tw_seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}
	surf->desktop->api.resize(surf, seat, serial, edge, user_data);
}

void
tw_desktop_surface_calc_window_geometry(struct tw_surface *surface,
                                        pixman_region32_t *geometry)
{
	pixman_region32_t region;
	struct tw_subsurface *sub;

	pixman_region32_init_rect(&region, 0, 0,
	                          surface->geometry.xywh.width,
	                          surface->geometry.xywh.height);
	wl_list_for_each(sub, &surface->subsurfaces, parent_link) {
		pixman_region32_t subregion;

		pixman_region32_init(&subregion);
		tw_desktop_surface_calc_window_geometry(sub->surface,
		                                        &subregion);
		pixman_region32_translate(&subregion, sub->sx, sub->sy);
		pixman_region32_union(&region, &region, &subregion);
		pixman_region32_fini(&subregion);
	}

	pixman_region32_copy(geometry, &region);
	pixman_region32_fini(&region);
}
