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

#include <sys/mman.h>
#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <os/file.h>
#include <os/os-compatibility.h>
#include <helpers.h>
#include <theme.h>
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
	struct wl_global *global;
	struct tw_config *config;
	//it can apply to many clients
	struct wl_list clients; //why do we need clients?
	struct tw_config_component_listener config_component;

	char *theme_path;

} THEME;


void
tw_theme_from_lua_script(struct tw_theme *theme, const char *script)
{

}

static void
theme_send_config(struct theme *theme)
{
	int fd;
	char *path = theme->theme_path;
	char *src;
	struct theme_client *client;

	if (!path)
		return;
	//you have to share a memory for copy
	FILE *stream = fopen(path, "r");
	size_t cr = ftell(stream);
	fseek(stream, 0, SEEK_END);
	size_t size = ftell(stream);

	fd = os_create_anonymous_file(size);
	if (fd < 0)
		goto out;
	src = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (src == MAP_FAILED)
		goto out;
	if (fread(src, size, 1, stream) != size)
		goto err_write;

	wl_list_for_each(client, &theme->clients, link)
		taiwins_theme_send_theme(client->resource, "theme", fd, size);

err_write:
	munmap(src, size);
out:
	close(fd);
	fseek(stream, cr, SEEK_SET);
	fclose(stream);
}
/*******************************************************************************
 * lua calls
 ******************************************************************************/

static int
_lua_read_theme(lua_State *L)
{
	struct theme *theme = &THEME;

	_lua_stackcheck(L, 2);
	const char *path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "invalid theme path %s", path);

	if (theme->theme_path)
		free(theme->theme_path);
	theme->theme_path = strdup(path);

	return 0;
}

static bool
init_theme_lua(struct tw_config *config, lua_State *L,
               struct tw_config_component_listener *comp)
{
	REGISTER_METHOD(L, "read_theme", _lua_read_theme);
	return true;
}

static void
apply_theme_lua(struct tw_config *c, bool cleanup,
                struct tw_config_component_listener *listener)
{
	struct theme *theme =
		container_of(listener, struct theme, config_component);

	if (theme->theme_path) {
		theme_send_config(theme);
		free(theme->theme_path);
		theme->theme_path = NULL;
	}
}

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

	// theme protocol does not have any
	taiwins_theme_send_theme(resource, "theme", -1, 100);

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
	      struct tw_config *config)
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

	wl_list_init(&THEME.config_component.link);
	THEME.config_component.apply = apply_theme_lua;
	THEME.config_component.init = init_theme_lua;

	tw_config_add_component(config, &THEME.config_component);
}
