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

#include <lauxlib.h>
#include <lua.h>
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

#include "lua_helper.h"
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

	struct tw_theme global_theme;

} THEME;

extern void tw_theme_init_for_lua(struct tw_theme *theme, lua_State *L);
extern int tw_theme_read(lua_State *L);

/*******************************************************************************
 * lua calls
 ******************************************************************************/


static bool
init_theme_lua(struct tw_config *config, lua_State *L,
               struct tw_config_component_listener *comp)
{
	struct theme *theme =
		container_of(comp, struct theme, config_component);
	struct tw_theme *tw_theme = &theme->global_theme;

	if (tw_theme->handle_pool.data)
		wl_array_release(&tw_theme->handle_pool);
	if (tw_theme->string_pool.data)
		wl_array_release(&tw_theme->string_pool);
	tw_theme_init_default(tw_theme);
	tw_theme_init_for_lua(tw_theme, L);

	return true;
}

static void
apply_theme_lua(struct tw_config *c, bool cleanup,
                struct tw_config_component_listener *listener)
{

	struct wl_resource *client;
	struct theme *theme =
		container_of(listener, struct theme, config_component);
	struct tw_theme *tw_theme = &theme->global_theme;
	int fd = tw_theme_to_fd(&theme->global_theme);
	if (fd > 0) {
		wl_list_for_each(client, &theme->clients, link)
			taiwins_theme_send_theme(client, "new theme", fd,
			                         sizeof(struct tw_theme) +
			                         tw_theme->handle_pool.size +
			                         tw_theme->string_pool.size);
	}
}


/**
 * @brief loading a lua theme from a lua script
 */
void
tw_theme_from_lua_script(struct tw_theme *theme, const char *script)
{
	lua_State *L;
	if (!(L = luaL_newstate()))
		return;
	//insert theme as light user data
	lua_pushlightuserdata(L, theme);
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme");


	//TODO I should also setup error functions
	if (!luaL_loadfile(L, script) || !lua_pcall(L, 0, 0, 0))
		return;

	lua_close(L);
}

/*******************************************************************************
 * wayland globals
 ******************************************************************************/

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
	memset(&THEME.global_theme, 0, sizeof(struct tw_theme));

	wl_list_init(&THEME.clients);

	wl_list_init(&THEME.compositor_destroy_listener.link);
	THEME.compositor_destroy_listener.notify = end_theme;
	wl_signal_add(&ec->destroy_signal, &THEME.compositor_destroy_listener);

	wl_list_init(&THEME.config_component.link);
	THEME.config_component.apply = apply_theme_lua;
	THEME.config_component.init = init_theme_lua;

	tw_config_add_component(config, &THEME.config_component);
}
