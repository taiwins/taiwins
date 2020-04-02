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
#include <wayland-server-core.h>
#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <os/file.h>
#include <os/os-compatibility.h>
#include <helpers.h>
#include <theme.h>
#include <wayland-util.h>

#include "taiwins.h"
#include "config.h"

struct shell;

struct theme {
	struct weston_compositor *ec;
	struct wl_listener compositor_destroy_listener;
	struct wl_global *global;
	struct tw_config *config;
	//it can apply to many clients
	struct wl_list clients; //why do we need clients?
	struct tw_config_component_listener config_component;

	struct tw_theme global_theme;
	int fd;

} THEME;

struct theme *
tw_theme_get_global(void)
{
	return &THEME;
}


static void
apply_theme_lua(struct tw_config *c, bool cleanup,
                struct tw_config_component_listener *listener)
{
	struct wl_resource *client;
	struct theme *theme =
		container_of(listener, struct theme, config_component);
	struct tw_theme *tw_theme = &theme->global_theme;

	if (cleanup)
		return;
	if (theme->fd > 0)
		close(theme->fd);

	theme->fd = tw_theme_to_fd(&theme->global_theme);
	if (theme->fd <= 0)
		return;
	if (wl_list_length(&theme->clients) == 0)
		return;

	wl_list_for_each(client, &theme->clients, link)
		taiwins_theme_send_theme(client, "new theme", theme->fd,
		                         sizeof(struct tw_theme) +
		                         tw_theme->handle_pool.size +
		                         tw_theme->string_pool.size);
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
bind_theme(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct theme *theme = data;
	struct tw_theme *tw_theme = &theme->global_theme;
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
		                         sizeof(struct tw_theme) +
		                         tw_theme->handle_pool.size +
		                         tw_theme->string_pool.size);
}

static void
end_theme(struct wl_listener *listener, void *data)
{
	struct theme *theme = container_of(listener, struct theme,
					   compositor_destroy_listener);
	wl_global_destroy(theme->global);

	if (theme->fd > 0)
		close(theme->fd);
	tw_theme_fini(&theme->global_theme);
}

/*******************************************************************************
 * public APIs
 ******************************************************************************/

struct tw_theme *
tw_theme_access_theme(struct theme *theme)
{
	return &theme->global_theme;
}


bool
tw_setup_theme(struct weston_compositor *ec, struct tw_config *config)
{
	THEME.ec = ec;
	THEME.global = wl_global_create(ec->wl_display,
	                                &taiwins_theme_interface,
					taiwins_theme_interface.version,
	                                &THEME,
					bind_theme);
	memset(&THEME.global_theme, 0, sizeof(struct tw_theme));

	wl_list_init(&THEME.clients);
	wl_list_init(&THEME.compositor_destroy_listener.link);

	THEME.compositor_destroy_listener.notify = end_theme;
	wl_signal_add(&ec->destroy_signal, &THEME.compositor_destroy_listener);

	wl_list_init(&THEME.config_component.link);
	THEME.config_component.apply = apply_theme_lua;
	tw_config_add_component(config, &THEME.config_component);

	return true;
}
