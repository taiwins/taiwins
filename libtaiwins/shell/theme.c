/*
 * theme.c - taiwins server theme api
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

#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <ctypes/os/file.h>
#include <ctypes/helpers.h>
#include <ctypes/os/os-compatibility.h>
#include <taiwins/objects/utils.h>
#include <twclient/theme.h>

struct shell;

struct tw_theme_global {
	struct wl_display *display;
	struct wl_listener compositor_destroy_listener;
	struct wl_global *global;
	struct tw_config *config;
	//it can apply to many clients
	struct wl_list clients; //why do we need clients?

	int fd;
	size_t theme_size;

} THEME;

struct tw_theme_global *
tw_theme_get_global(void)
{
	return &THEME;
}


WL_EXPORT void
tw_theme_notify(struct tw_theme_global *theme, struct tw_theme *new_theme)
{
	struct wl_resource *client;
	if (!new_theme)
		return;

	if (theme->fd > 0)
		close(theme->fd);
	theme->fd = tw_theme_to_fd(new_theme);
	if (theme->fd <= 0)
		goto end;

	theme->theme_size = sizeof(struct tw_theme) +
		new_theme->handle_pool.size +
		new_theme->string_pool.size;

	if (wl_list_length(&theme->clients) == 0)
		goto end;
	wl_list_for_each(client, &theme->clients, link)
		taiwins_theme_send_theme(client, "new theme", theme->fd,
		                         theme->theme_size);
end:
	tw_theme_fini(new_theme);
	free(new_theme);
}

/*******************************************************************************
 * wayland globals
 ******************************************************************************/

static void
unbind_theme(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
bind_theme(struct wl_client *client, void *data, UNUSED_ARG(uint32_t version),
           uint32_t id)
{
	struct tw_theme_global *theme = data;
	struct wl_resource *resource =
		wl_resource_create(client, &taiwins_theme_interface,
				   taiwins_theme_interface.version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	//theme does not have requests
	wl_resource_set_implementation(resource, NULL, NULL, unbind_theme);
	wl_list_insert(&theme->clients, wl_resource_get_link(resource));

	if (theme->fd > 0)
		taiwins_theme_send_theme(resource, "new_theme", theme->fd,
		                         theme->theme_size);
	/* taiwins_theme_send_cursor(resource, "whiteglass", 24); */
}

static void
end_theme(struct wl_listener *listener, void *data)
{
	struct tw_theme_global *theme =
		container_of(listener, struct tw_theme_global,
		             compositor_destroy_listener);
	wl_global_destroy(theme->global);

	if (theme->fd > 0)
		close(theme->fd);
}

/*******************************************************************************
 * public APIs
 ******************************************************************************/

WL_EXPORT struct tw_theme_global *
tw_theme_create_global(struct wl_display *display)
{
	THEME.display = display;
	THEME.global = wl_global_create(display,
	                                &taiwins_theme_interface,
	                                taiwins_theme_interface.version,
	                                &THEME,
	                                bind_theme);
	wl_list_init(&THEME.clients);

	tw_set_display_destroy_listener(display,
	                                &THEME.compositor_destroy_listener,
	                                end_theme);
	return &THEME;
}
