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
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wayland-xdg-shell-server-protocol.h>

#include "ctypes/helpers.h"
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

	switch (option) {
	case TW_DESKTOP_INIT_INCLUDE_WL_SHELL:
		ret = ret && init_wl_shell(desktop);
		break;
	case TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL:
		ret = ret && init_xdg_shell(desktop);
		break;
	}
	if (!ret)
		tw_desktop_fini(desktop);

	wl_list_init(&desktop->destroy_listener.link);
	desktop->destroy_listener.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &desktop->destroy_listener);

	return ret;
}

void
tw_desktop_surface_init(struct tw_desktop_surface *surf,
                        struct wl_resource *resource,
                        struct tw_desktop_manager *desktop)
{
	surf->desktop = desktop;
	surf->shell_surface = resource;
	surf->fullscreened = false;
	surf->maximized = false;
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
