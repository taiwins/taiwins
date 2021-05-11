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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <wayland-server.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/subsurface.h>
#include <taiwins/objects/seat.h>

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
		wl_container_of(listener, desktop, destroy_listener);

	tw_desktop_fini(desktop);
}

WL_EXPORT bool
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

WL_EXPORT struct tw_desktop_manager *
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

WL_EXPORT void
tw_desktop_surface_init(struct tw_desktop_surface *surf,
                        struct tw_surface *surface,
                        struct wl_resource *resource,
                        struct tw_desktop_manager *desktop)
{
	surf->desktop = desktop;
	surf->tw_surface = surface;
	surf->resource = resource;
	surf->surface_added = false;
	surf->title = NULL;
	surf->class = NULL;
	surf->states = 0;
	surf->max_size.w = UINT32_MAX;
	surf->max_size.h = UINT32_MAX;
	surf->min_size.w = 0;
	surf->min_size.h = 0;
}

WL_EXPORT void
tw_desktop_surface_fini(struct tw_desktop_surface *surf)
{
	if (surf->title) {
		free(surf->title);
		surf->title = NULL;
	}
	if (surf->class) {
		free(surf->class);
		surf->class = NULL;
	}
}

WL_EXPORT void
tw_desktop_surface_add(struct tw_desktop_surface *surf)
{
	void *user_data = surf->desktop->user_data;

	if (!surf->surface_added) {
		surf->desktop->api.surface_added(surf, user_data);
		surf->surface_added = true;
	}
}

WL_EXPORT void
tw_desktop_surface_rm(struct tw_desktop_surface *surf)
{
	void *user_data = surf->desktop->user_data;
	if (surf->surface_added && surf->tw_surface) {
		surf->desktop->api.surface_removed(surf, user_data);
		surf->surface_added = false;
	}
}

WL_EXPORT void
tw_desktop_surface_set_fullscreen(struct tw_desktop_surface *surf,
                                  struct wl_resource *output,
                                  bool fullscreen)
{
	void *user_data = surf->desktop->user_data;
	if ((surf->states & TW_DESKTOP_SURFACE_FULLSCREENED) != fullscreen)
		surf->desktop->api.fullscreen_requested(surf, output,
		                                        fullscreen, user_data);
}

WL_EXPORT void
tw_desktop_surface_set_maximized(struct tw_desktop_surface *surf,
                                 bool maximized)
{
	void *user_data = surf->desktop->user_data;
	if ((surf->states & TW_DESKTOP_SURFACE_MAXIMIZED) != maximized)
		surf->desktop->api.maximized_requested(surf, maximized,
		                                       user_data);
}

WL_EXPORT void
tw_desktop_surface_set_minimized(struct tw_desktop_surface *surf)
{
	void *user_data = surf->desktop->user_data;
	if (!(surf->states & TW_DESKTOP_SURFACE_MINIMIZED))
		surf->desktop->api.minimized_requested(surf, user_data);
}

WL_EXPORT void
tw_desktop_surface_set_title(struct tw_desktop_surface *surf,
                             const char *title, size_t maxlen)
{
	char *tmp = NULL;
	if (title) {
		tmp = maxlen ? strndup(title, maxlen) : strdup(title);
		if (!tmp)
			return;
	}
	free(surf->title);
	surf->title = tmp;
}

WL_EXPORT void
tw_desktop_surface_set_class(struct tw_desktop_surface *surf,
                             const char *class, size_t maxlen)
{
	char *tmp = NULL;
	if (class) {
		tmp = maxlen ? strndup(class, maxlen) : strdup(class);
		if (!tmp)
			return;
	}
	free(surf->class);
	surf->class = tmp;
}

WL_EXPORT void
tw_desktop_surface_move(struct tw_desktop_surface *surf,
                        struct tw_seat *seat, uint32_t serial)
{
	void *user_data = surf->desktop->user_data;

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}
	surf->desktop->api.move(surf, seat, serial, user_data);
}

WL_EXPORT void
tw_desktop_surface_resize(struct tw_desktop_surface *surf,
                          struct tw_seat *seat, uint32_t edge,
                          uint32_t serial)
{
	void *user_data = surf->desktop->user_data;

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("invalid serial %u", serial);
		return;
	}
	surf->desktop->api.resize(surf, seat, serial, edge, user_data);
}

WL_EXPORT void
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

		//avoid exotic subsurfaces
		if (!tw_surface_is_subsurface(sub->surface))
			continue;
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

WL_EXPORT void
tw_desktop_surface_send_configure(struct tw_desktop_surface *dsurf,
                                  enum wl_shell_surface_resize edge,
                                  int x, int y, unsigned w, unsigned h,
                                  uint32_t state_flags)
{
	dsurf->states = state_flags & TW_DESKTOP_SURFACE_STATES;
	dsurf->configure(dsurf, edge, x, y, w, h,
	                 state_flags & (~TW_DESKTOP_SURFACE_STATES));
}

WL_EXPORT void
tw_desktop_surface_send_ping(struct tw_desktop_surface *dsurf, uint32_t serial)
{
	dsurf->ping(dsurf, serial);
}

WL_EXPORT void
tw_desktop_surface_send_close(struct tw_desktop_surface *dsurf)
{
	dsurf->close(dsurf);
}

bool
tw_surface_is_wl_shell_surface(struct tw_surface *surface);

bool
tw_surface_is_xdg_surface(struct tw_surface *surface);

WL_EXPORT struct tw_desktop_surface *
tw_desktop_surface_from_tw_surface(struct tw_surface *surface)
{
	if (tw_surface_is_wl_shell_surface(surface) ||
	    tw_surface_is_xdg_surface(surface))
		return surface->role.commit_private;
	else
		return NULL;
}
