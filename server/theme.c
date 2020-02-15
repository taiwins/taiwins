/*
 * theme.h - taiwins server theme header
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

#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <helpers.h>
#include "taiwins.h"
#include "config.h"

struct shell;

struct theme_client {
	struct wl_resource *resource;
	struct wl_list link;
};

struct theme {
	struct weston_compositor *ec;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener config_component;
	struct wl_global *global;
	//it can apply to many clients
	struct wl_list clients;
	//now you would include taiwins_theme

} THEME;


static void
unbind_theme(struct wl_resource *resource)
{
	struct theme_client *tc = wl_resource_get_user_data(resource);
	wl_list_remove(&tc->link);
	free(tc);
}

static void
bind_theme(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct theme *theme = data;
	struct theme_client *tc;
	struct wl_resource *resource =
		wl_resource_create(client, &taiwins_theme_interface,
				   taiwins_theme_interface.version, id);
	tc = zalloc(sizeof(struct theme_client));
	if (!tc) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_NO_MEMORY,
				       "failed to create theme for client %d",
		                       id);
		wl_resource_destroy(resource);
	}
	//theme does not have requests
	wl_resource_set_implementation(resource, NULL, tc, unbind_theme);

	tc->resource = resource;
	wl_list_init(&tc->link);
	wl_list_insert(&theme->clients, &tc->link);
}

static void
end_theme(struct wl_listener *listener, void *data)
{
	struct theme *theme = container_of(listener, struct theme,
					   compositor_destroy_listener);
	wl_global_destroy(theme->global);
}

void
annouce_theme(struct weston_compositor *ec, struct shell *shell,
	      struct taiwins_config *config)
{
	THEME.ec = ec;
	THEME.global = wl_global_create(ec->wl_display,
	                                &taiwins_theme_interface,
					taiwins_theme_interface.version,
	                                &THEME,
					bind_theme);
	wl_list_init(&THEME.clients);

	wl_list_init(&THEME.compositor_destroy_listener.link);
	THEME.compositor_destroy_listener.notify = end_theme;
	wl_signal_add(&ec->destroy_signal, &THEME.compositor_destroy_listener);
}
