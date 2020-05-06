/*
 * shell_msg.c - taiwins client shell lua config implementation
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

#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <client.h>
#include "shell.h"

#define REGISTRY_SHELL "_shell"


static inline struct desktop_shell *
_lua_to_shell(lua_State *L)
{
	struct desktop_shell *shell;
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);
	shell = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return shell;
}


static int
_lua_set_wallpaper(lua_State *L)
{
	struct desktop_shell *shell = _lua_to_shell(L);
	const char *path;

	tw_lua_stackcheck(L, 2);
	path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "wallpaper does not exist!");
	SET_PENDING_STR(&t->background_path, strdup(path));
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_set_widgets(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
	const char *path;

	tw_lua_stackcheck(L, 2);
	path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "widget path does not exist!");
	SET_PENDING_STR(&t->widgets_path, strdup(path));
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_set_panel_position(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
	const char *pos;

        luaL_checktype(L, 2, LUA_TSTRING);
	pos = lua_tostring(L, 2);
	if (strcmp(pos, "bottom") == 0) {
		SET_PENDING(&t->panel_pos, pos,
		            TAIWINS_SHELL_PANEL_POS_BOTTOM);
		tw_config_table_dirty(t, true);
	} else if (strcmp(pos, "top") == 0) {
		SET_PENDING(&t->panel_pos, pos,
		            TAIWINS_SHELL_PANEL_POS_TOP);
		tw_config_table_dirty(t, true);
	} else
		luaL_error(L, "invalid panel position %s", pos);
	return 0;
}

static bool
_lua_is_menu_item(struct lua_State *L, int idx)
{
	if (lua_rawlen(L, idx) != 2)
		return false;
	size_t len[2] = {TAIWINS_MAX_MENU_ITEM_NAME,
	                 TAIWINS_MAX_MENU_CMD_LEN};
	for (int i = 0; i < 2; ++i) {
		lua_rawgeti(L, idx, i+1);
		const char *value = (lua_type(L, -1) == LUA_TSTRING) ?
			lua_tostring(L, -1) : NULL;
		if (value == NULL || strlen(value) >= (len[i]-1)) {
			lua_pop(L, 1);
			return false;
		}
		lua_pop(L, 1);
	}
	return true;
}

static bool
_lua_parse_menu(struct lua_State *L, vector_t *menus)
{
	bool parsed = true;
	struct tw_menu_item menu_item = {
		.has_submenu = false,
		.len = 0};
	if (_lua_is_menu_item(L, -1)) {
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		strop_ncpy(menu_item.endnode.title, lua_tostring(L, -2),
			TAIWINS_MAX_MENU_ITEM_NAME);
		strop_ncpy(menu_item.endnode.cmd, lua_tostring(L, -1),
			TAIWINS_MAX_MENU_CMD_LEN);
		lua_pop(L, 2);
		vector_append(menus, &menu_item);
	} else if (lua_istable(L, -1)) {
		int n = lua_rawlen(L, -1);
		int currlen = menus->len;
		for (int i = 1; i <= n && parsed; i++) {
			lua_rawgeti(L, -1, i);
			parsed = parsed && _lua_parse_menu(L, menus);
			lua_pop(L, 1);
		}
		if (parsed) {
			menu_item.has_submenu = true;
			menu_item.len = menus->len - currlen;
			vector_append(menus, &menu_item);
		}
	} else
		return false;
	return parsed;
}

static int
_lua_set_menus(lua_State *L)
{
	vector_t menu;
	struct tw_config_table *t = _lua_to_config_table(L);

	vector_init_zero(&menu, sizeof(struct tw_menu_item), NULL);
	tw_lua_stackcheck(L, 2);
	luaL_checktype(L, 2, LUA_TTABLE);
	if (!_lua_parse_menu(L, &menu)) {
		vector_destroy(&menu);
		return luaL_error(L, "error parsing menus.");
	}
	SET_PENDING_VEC(&t->menu, &menu);
	tw_config_table_dirty(t, true);
	return 0;
}



static int
luaopen_taiwins_shell(lua_State *L)
{
	return 1;
}


void *
desktop_shell_init_lua(struct desktop_shell *shell)
{
	lua_State *L = luaL_newstate();
	if (!L)
		return NULL;
	luaL_openlibs(L);

	luaL_requiref(L, "taiwins_shell", luaopen_taiwins_shell, true);

	return L;
}
