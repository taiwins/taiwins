/*
 * config_lua.c - taiwins config lua bindings
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

#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <libweston/libweston.h>

#include <strops.h>
#include <os/file.h>
#include <vector.h>
#include <helpers.h>

#include "lua_helper.h"
#include "config_internal.h"

static inline void
_lua_error(struct tw_config *config, const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	config->print(fmt, argp);
	va_end(argp);
}

static inline struct tw_config *
to_user_config(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__config");
	struct tw_config *c = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return c;
}

/*******************************************************************************
 * binding functions
 ******************************************************************************/

static inline void
_lua_run_binding(void *data)
{
	struct tw_binding *b = data;
	lua_State *L = b->user_data;
	struct tw_config *config;

	lua_getfield(L, LUA_REGISTRYINDEX, b->name);
	if (lua_pcall(L, 0, 0, 0)) {
		config = to_user_config(L);
		_lua_error(config, "error calling lua bindings\n");
	}
	lua_settop(L, 0);
}

static void
_lua_run_keybinding(UNUSED_ARG(struct weston_keyboard *keyboard),
                    UNUSED_ARG(const struct timespec *time),
                    UNUSED_ARG(uint32_t key), UNUSED_ARG(uint32_t option),
                    void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_btnbinding(UNUSED_ARG(struct weston_pointer *pointer),
                    UNUSED_ARG(const struct timespec *time),
                    UNUSED_ARG(uint32_t btn), void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_axisbinding(UNUSED_ARG(struct weston_pointer *pointer),
                     UNUSED_ARG(const struct timespec *time),
                     UNUSED_ARG(struct weston_pointer_axis_event *event),
		     void *data)
{
	_lua_run_binding(data);
}


static struct tw_binding *
_new_lua_binding(struct tw_config *config, enum tw_binding_type type)
{
	struct tw_binding *b = vector_newelem(&config->config_bindings);
	b->user_data = config->user_data;
	b->type = type;
	sprintf(b->name, "luabinding_%x", config->config_bindings.len);
	switch (type) {
	case TW_BINDING_key:
		b->key_func = _lua_run_keybinding;
		break;
	case TW_BINDING_btn:
		b->btn_func = _lua_run_btnbinding;
		break;
	case TW_BINDING_axis:
		b->axis_func = _lua_run_axisbinding;
	default:
		break;
	}
	return b;
}

static bool
_parse_binding(struct tw_binding *b, const char *seq_string)
{
	char seq_copy[128];
	strop_ncpy(seq_copy, seq_string, 128);
	char *save_ptr;
	char *c = strtok_r(seq_copy, " ,;", &save_ptr);
	int count = 0;
	bool parsed = true;
	while (c != NULL && count < 5 && parsed) {
		uint32_t mod, code;
		parsed = parsed &&
			parse_one_press(c, b->type, &mod, &code);

		switch (b->type) {
		case TW_BINDING_key:
			b->keypress[count].keycode = code;
			b->keypress[count].modifier = mod;
			break;
		case TW_BINDING_btn:
			b->btnpress.btn = code;
			b->btnpress.modifier = mod;
			break;
		case TW_BINDING_axis:
			b->axisaction.axis_event = code;
			b->axisaction.modifier = mod;
			break;
		default: //we dont deal with touch right now
			break;
		}

		c = strtok_r(NULL, " ,;", &save_ptr);
		count += (parsed) ? 1 : 0;
		if (count > 1 && b->type != TW_BINDING_key)
			parsed = false;
	}
	if (count >= 5)
		return false;
	//clean the rest of the bits
	for (int i = count; i < 5; i++) {
		b->keypress[count].keycode = 0;
		b->keypress[count].modifier = 0;
	}
	return true && parsed;
}

static inline struct tw_binding *
_find_default_binding(struct tw_config *config, const char *name)
{
	for (int i = 0; i < TW_BUILTIN_BINDING_SIZE; i++) {
		if (strcmp(config->builtin_bindings[i].name, name) == 0)
			return &config->builtin_bindings[i];
	}
	return NULL;
}

static inline const char *
_binding_type_name(enum tw_binding_type type)
{
	static const char *names[] = {"key", "btn", "axis"};
	switch (type) {
	case TW_BINDING_key:
		return names[0];
	case TW_BINDING_btn:
		return names[1];
	case TW_BINDING_axis:
		return names[2];
	default:
		return NULL;
	};
}

static inline int
_lua_bind(lua_State *L, enum tw_binding_type binding_type)
{
	struct tw_config *cd = to_user_config(L);
	struct tw_binding *binding_to_find = NULL;
	const char *key = NULL;
	struct tw_binding temp = {0};
	const char *binding_seq = lua_tostring(L, 3);
	const char *type_name = _binding_type_name(binding_type);

	temp.type = binding_type;
	if (!binding_seq || !_parse_binding(&temp, binding_seq))
		return luaL_error(L, "bind_%s:invalid binding sequence\n",
		                  type_name);
	//builtin binding
	if (tw_lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		binding_to_find = _find_default_binding(cd, key);
		if (!binding_to_find || binding_to_find->type != binding_type)
			return luaL_error(L, "bind_%s:binding %s not found\n",
			                  type_name, key);
	}
	//user binding
	else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
		//create a function in the registry so we can call it later.
		binding_to_find = _new_lua_binding(cd, binding_type);
		lua_pushvalue(L, 2);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding
	} else
		return luaL_error(L, "bind_%s: invalid argument\n",
		                  type_name);

	//now we copy the binding seq to
	memcpy(binding_to_find->keypress, temp.keypress,
	       sizeof(temp.keypress));
	return 0;
}

static int
_lua_bind_key(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_key);
}

static int
_lua_bind_btn(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_btn);
}

static int
_lua_bind_axis(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_axis);
}

static int
_lua_bind_tch(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_tch);
}

 /******************************************************************************
 * main config
 *****************************************************************************/

static tw_config_transform_t TRANSFORMS[] = {
	{0, false, WL_OUTPUT_TRANSFORM_NORMAL},
	{90, false, WL_OUTPUT_TRANSFORM_90},
	{180, false, WL_OUTPUT_TRANSFORM_180},
	{270, false, WL_OUTPUT_TRANSFORM_270},
	{0, true, WL_OUTPUT_TRANSFORM_FLIPPED},
	{90, true, WL_OUTPUT_TRANSFORM_FLIPPED_90},
	{180, true, WL_OUTPUT_TRANSFORM_FLIPPED_180},
	{270, true, WL_OUTPUT_TRANSFORM_FLIPPED_270},
};

#define REGISTRY_COMPOSITOR "__compositor"
#define REGISTRY_CONFIG "__config"
#define REGISTRY_WORKSPACES "__workspaces"
#define REGISTRY_HINT "__hint"
#define CONFIG_TABLE "__config_table"
#define CONFIG_WESTON_OUTPUT "_weston_output"

#define METATABLE_COMPOSITOR "metatable_compositor"
#define METATABLE_OUTPUT "metatable_output"
#define METATABLE_WORKSPACE "metatable_workspace"
#define METATABLE_SHELL "metatable_shell"

static struct weston_compositor *
_lua_to_compositor(lua_State *L)
{
	struct weston_compositor *ec;

	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_COMPOSITOR);
	ec = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return ec;
}

static inline struct tw_backend *
_lua_to_backend(lua_State *L)
{
	struct tw_config *config;
	struct tw_backend *backend;
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CONFIG);
	config = lua_touserdata(L, -1);
	lua_pop(L, 1);
	backend = tw_config_request_object(config, "backend");
	if (!backend)
		luaL_error(L, "taiwins backend not available yet\n");

	return backend;
}

static inline struct desktop*
_lua_to_desktop(lua_State *L)
{
	struct desktop *desktop;
	struct tw_config *config;

        lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CONFIG);
	config = lua_touserdata(L, -1);
	lua_pop(L, 1);
	desktop = tw_config_request_object(config, "desktop");
	if (!desktop)
		luaL_error(L, "taiwins desktop not available yet\n");
	return desktop;
}

static inline struct tw_theme *
_lua_to_theme(lua_State *L)
{
	struct tw_theme *theme;
	struct tw_config *config;

	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CONFIG);
	config = lua_touserdata(L, -1);
	lua_pop(L, 1);
	theme = tw_config_request_object(config, "theme");
	if (!theme)
		luaL_error(L, "taiwins theme not available yet\n");

	return theme;
}


static inline struct tw_config_table *
_lua_to_config_table(lua_State *L)
{
	struct tw_config_table *table;

	lua_getfield(L, LUA_REGISTRYINDEX, CONFIG_TABLE);
	table = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return table;
}

extern int tw_theme_read(lua_State *L);

/******************************************************************************
 * backend configs
 *****************************************************************************/

/**
 * @brief request a output from global output. Push a table on stack if the
 * output exists. Otherwise stack unchanged.
 *
 * TODO : I do not know what is use of this
 * [-0, +(1|0), -]
 */
static struct weston_output *
_lua_get_output(lua_State *L, int pos)
{
	struct weston_output *weston_output;

	if (!tw_lua_istable(L, pos, METATABLE_OUTPUT))
		return NULL;
	lua_getfield(L, pos, CONFIG_WESTON_OUTPUT);
	weston_output = lua_touserdata(L, -1);
	lua_pop(L, 1);

	return weston_output;
}

static int
_lua_is_under_x11(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	lua_pushboolean(L, (tw_backend_get_type(b) == WESTON_BACKEND_X11));
	return 1;
}

static int
_lua_is_under_wayland(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	lua_pushboolean(L, (tw_backend_get_type(b) == WESTON_BACKEND_WAYLAND));
	return 1;
}

static int
_lua_is_windowed_display(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	int type = tw_backend_get_type(b);
	lua_pushboolean(L, (type == WESTON_BACKEND_X11 ||
			    type == WESTON_BACKEND_WAYLAND ||
			    type == WESTON_BACKEND_RDP ||
			    type == WESTON_BACKEND_HEADLESS));
	return 1;
}

static int
_lua_get_windowed_output(lua_State *L)
{
	struct weston_compositor *ec;
	struct weston_output *output;
	struct tw_backend *backend = _lua_to_backend(L);
	int bkend_type;

	ec = _lua_to_compositor(L);
	bkend_type = tw_backend_get_type(backend);
	if (bkend_type != WESTON_BACKEND_X11 &&
	    bkend_type != WESTON_BACKEND_WAYLAND &&
	    bkend_type != WESTON_BACKEND_RDP &&
	    bkend_type != WESTON_BACKEND_HEADLESS) {
		return luaL_error(L, "no windowed output available");
	} else {
		output = tw_get_default_output(ec);
		if (!output)
			return luaL_error(L, "%s: no window output available",
			                  "get_window_display");
	}
	lua_newtable(L);
	lua_pushlightuserdata(L, output);
	lua_setfield(L, -2, CONFIG_WESTON_OUTPUT);
	luaL_getmetatable(L, METATABLE_OUTPUT);
	lua_setmetatable(L, -2);
	return 1;
}

static inline enum wl_output_transform
_lua_output_transfrom_from_value(lua_State *L, int rotate, bool flip)
{
	for (unsigned i = 0; i < NUMOF(TRANSFORMS); i++)
		if (TRANSFORMS[i].rotate == rotate &&
		    TRANSFORMS[i].flip == flip)
			return TRANSFORMS[i].t;
	return luaL_error(L, "invalid transforms option.");
}

/**
 * @brief lua rotate flip output
 *
 * does not change on config run
 */
static int
_lua_output_rotate_flip(lua_State *L)
{
	bool dirty = false;
	int rotate;
	bool flip;
	tw_config_transform_t transform;
	struct weston_output *output;
	struct tw_config_table *t = _lua_to_config_table(L);

	if (!tw_lua_istable(L, 1, METATABLE_OUTPUT))
		return luaL_error(L, "%s: invaild output\n",
		                  "output.rotate_flip");
	output = _lua_get_output(L, 1);

	if (lua_gettop(L) == 1) {
		transform = TRANSFORMS[output->transform];
		lua_pushinteger(L, transform.rotate);
		lua_pushboolean(L, transform.flip);
		return 2;
	} else if(lua_gettop(L) == 2) {
		rotate = luaL_checkinteger(L, 2);
		flip = false;
		dirty = true;
	} else if (lua_gettop(L) == 3) {
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		rotate = luaL_checkinteger(L, 2);
		flip = lua_toboolean(L, 3);
		dirty = true;
	} else
		return luaL_error(L, "%s.%s: invalid number of arguments",
		                  output->name, "rotate_flip");

	if (dirty) {
		transform.t = _lua_output_transfrom_from_value(L, rotate, flip);
		SET_PENDING(&t->outputs[output->id].transform,
		            transform, transform.t);
		tw_config_table_dirty(t, dirty);
	}
	return 0;
}

static int
_lua_output_scale(lua_State *L)
{
	bool dirty = false;
	unsigned int scale;
	struct weston_output *output = _lua_get_output(L, 1);
	struct tw_config_table *t = _lua_to_config_table(L);

	if (!output)
		return luaL_error(L, "outut.scale: invalid output\n");
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, output->scale);
		return 1;
	} else if (lua_gettop(L) == 2) {
		tw_lua_stackcheck(L, 2);
		scale = luaL_checkinteger(L, 2);
		if (scale <= 0 || scale > 4)
			return luaL_error(L, "%s.scale(): invalid display scale",
			                  output->name);
		dirty = true;
	} else
		return luaL_error(L, "%s.scale: invalid num arguments\n",
			output->name);
	if (dirty) {
		SET_PENDING(&(t->outputs[output->id].scale), val, scale);
		tw_config_table_dirty(t, dirty);
	}
	return 0;
}

static int
_lua_output_resolution(lua_State *L)
{
	struct weston_output *output =
		_lua_get_output(L, 1);
	if (!output)
		return luaL_error(L, "output.resolution: invalid output\n");
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, output->width);
		lua_pushinteger(L, output->height);
		return 2;
	} else {
		return luaL_error(L, "output.resolution: not implemented\n");
	}
}

static int
_lua_output_position(lua_State *L)
{
	struct weston_output *output =
		_lua_get_output(L, 1);
	if (!output)
		return luaL_error(L, "output.position: invalid output\n");

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, output->x);
		lua_pushinteger(L, output->y);
		return 2;
	} else {
		//TODO we deal with this later.
		tw_lua_stackcheck(L, 2);
		return 0;
	}
}

/******************************************************************************
 * shell config
 *****************************************************************************/

static int
_lua_set_wallpaper(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
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
_lua_set_sleep_timer(lua_State *L)
{
	(void)L;
	return 0;
}

static int
_lua_set_lock_timer(lua_State *L)
{
	int32_t seconds;
	struct tw_config_table *t =
		_lua_to_config_table(L);
	tw_lua_stackcheck(L, 2);
	seconds = luaL_checknumber(L, 2);
	if (seconds < 0)
		return luaL_error(L, "%s:idle time is negative.",
		                  "shell.sleep_in");

	SET_PENDING(&t->lock_timer, val, seconds);
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_request_shell(lua_State *L)
{
	/* this function will be replaced into config */
	lua_newtable(L);
	luaL_getmetatable(L, METATABLE_SHELL);
	lua_setmetatable(L, -2);
	return 1;
}

/******************************************************************************
 * desktop config
 *****************************************************************************/

static int
_lua_set_ws_layout(lua_State *L)
{
	int index;
	const char *layout;
	struct desktop *d = _lua_to_desktop(L);
	struct tw_config_table *table =
		_lua_to_config_table(L);

	if (!tw_lua_istable(L, 1, METATABLE_WORKSPACE))
		return luaL_error(L, "%s: invalid workspace\n",
		                  "workspace.set_layout");
	lua_getfield(L, 1, "index");
	index = lua_tonumber(L, -1);
	lua_pop(L, 1);
	luaL_checktype(L, 2, LUA_TSTRING);
	layout = lua_tostring(L, 2);
	if (index < 0 || index >= tw_desktop_num_workspaces(d))
		return luaL_error(L, "%s: invaild workpsace\n",
		                  "workspace.set_layout");
	if (strcmp(layout, "floating") == 0)
		SET_PENDING(&table->workspaces[index].layout,
		            layout, LAYOUT_FLOATING);
	else if (strcmp(layout, "tiling") == 0)
		SET_PENDING(&table->workspaces[index].layout,
		            layout, LAYOUT_TILING);
	else
		return luaL_error(L, "%s: invaild layout\n",
		                  "workspace.set_layout");
	tw_config_table_dirty(table, true);
	return 0;
}

static int
_lua_desktop_gap(lua_State *L)
{
	int inner, outer;
	struct desktop *d = _lua_to_desktop(L);
	struct tw_config_table * t =
		_lua_to_config_table(L);

	if (lua_gettop(L) == 1) {
		tw_desktop_get_gap(d, &inner, &outer);
		lua_pushinteger(L, inner);
		lua_pushinteger(L, outer);
		return 2;
	} else if (lua_gettop(L) == 3) {
		inner = luaL_checkinteger(L, 2);
		outer = luaL_checkinteger(L, 3);
		if (inner < 0 || inner > 100 ||
		    outer < 0 || outer > 100)
			return luaL_error(L, "invalid size of gaps.");
		SET_PENDING(&t->desktop_igap, val, inner);
		SET_PENDING(&t->desktop_ogap, val, outer);
		tw_config_table_dirty(t, true);
		return 0;
	}
	return luaL_error(L, "invalid size of params for gap.");
}

static int
_lua_request_workspaces(lua_State *L)
{
	struct desktop *d = _lua_to_desktop(L);

	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WORKSPACES);
	if (lua_istable(L, -1))
		return 1;
	//create workspaces if not created
        lua_pop(L, 1);
	lua_newtable(L); //1
	for (int i = 0; i < tw_desktop_num_workspaces(d) ; i++) {

		lua_newtable(L); //2
		luaL_getmetatable(L, METATABLE_WORKSPACE); //3
		lua_setmetatable(L, -2); //2

		lua_pushstring(L, "layout"); //3
		lua_pushstring(L, tw_desktop_get_workspace_layout(d, i)); //4
		lua_settable(L, -3); //2

		lua_pushstring(L, "index"); //3
		lua_pushnumber(L, i); //4
		lua_settable(L, -3); //2


		lua_rawseti(L, -2, i+1); //1
	}
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_WORKSPACES);
        return 1;
}

/******************************************************************************
 * tw_xwayland
 *****************************************************************************/

static int
_lua_enable_xwayland(lua_State *L)
{
	bool val;
	struct tw_config_table *t =
		_lua_to_config_table(L);

	tw_lua_assert(L, lua_gettop(L) == 2,
	              "xwayland: invalid number of arguments");
        val = lua_toboolean(L, 2);
        SET_PENDING(&t->xwayland, enable, val);
        tw_config_table_dirty(t, true);
	return 0;
}

/******************************************************************************
 * global config
 *****************************************************************************/

//TODO: there are possible leaks if you run config multiple times!
//TODO: its is better to set xkb rules together.
static int
_lua_set_keyboard_model(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

        tw_lua_stackcheck(L, 2);
	t->xkb_rules.model = strdup(luaL_checkstring(L, 2));
	return 0;
}

static int
_lua_set_keyboard_layout(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

        tw_lua_stackcheck(L, 2);
	t->xkb_rules.layout = strdup(luaL_checkstring(L, 2));
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_set_keyboard_options(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

        tw_lua_stackcheck(L, 2);
	t->xkb_rules.options = strdup(luaL_checkstring(L, 2));
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_set_repeat_info(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
	int32_t rate;
	int32_t delay;

        tw_lua_stackcheck(L, 3);
	rate = luaL_checknumber(L, 2);
	delay = luaL_checknumber(L, 3);
	SET_PENDING(&t->kb_delay, val, delay);
	SET_PENDING(&t->kb_repeat, val, rate);
	tw_config_table_dirty(t, true);
	return 0;
}


static int
_lua_wake_compositor(lua_State *L)
{
	struct tw_config *config;
	tw_lua_stackcheck(L, 1);
	if (!tw_lua_istable(L, 1, METATABLE_COMPOSITOR))
		return luaL_error(L, "%s: expecting compositor object\n",
		                  "compositor.wake");
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CONFIG);
	config = lua_touserdata(L, -1);
	lua_pop(L, 1);
	tw_config_wake_compositor(config);
	return 0;
}

static int
_lua_read_theme(lua_State *L)
{
	struct tw_config_table *table = _lua_to_config_table(L);
	struct tw_theme *theme;
	//required for tw_theme_read
        theme = _lua_to_theme(L);
	lua_pushlightuserdata(L, theme);
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme");

	tw_theme_read(L);
	SET_PENDING(&table->theme, read, true);
	tw_config_table_dirty(table, true);

	return 0;
}

static int
_lua_get_config(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, METATABLE_COMPOSITOR);
	lua_setmetatable(L, -2);
	return 1;
}

static int
luaopen_taiwins(lua_State *L)
{
	////////////////////// backend ///////////////////////////////
	luaL_newmetatable(L, METATABLE_OUTPUT);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	REGISTER_METHOD(L, "rotate_flip", _lua_output_rotate_flip);
	REGISTER_METHOD(L, "scale", _lua_output_scale);
	REGISTER_METHOD(L, "resolution", _lua_output_resolution);
	REGISTER_METHOD(L, "position", _lua_output_position);
	lua_pop(L, 1);

	////////////////////// shell  ///////////////////////////////
	luaL_newmetatable(L, METATABLE_SHELL);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	REGISTER_METHOD(L, "set_wallpaper", _lua_set_wallpaper);
	REGISTER_METHOD(L, "init_widgets", _lua_set_widgets);
	REGISTER_METHOD(L, "panel_position", _lua_set_panel_position);
	REGISTER_METHOD(L, "set_menus", _lua_set_menus);
	lua_pop(L, 1);

        ////////////////////// desktop //////////////////////////////
	//metatable for workspace
	luaL_newmetatable(L, METATABLE_WORKSPACE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	REGISTER_METHOD(L, "set_layout", _lua_set_ws_layout);
	lua_pop(L, 1);

	////////////////////// global compositor ///////////////////
	luaL_newmetatable(L, METATABLE_COMPOSITOR); //s1
	lua_pushvalue(L, -1); //s2
	lua_setfield(L, -2, "__index"); //s1
	// normal configurations
	REGISTER_METHOD(L, "bind_key", _lua_bind_key);
	REGISTER_METHOD(L, "bind_btn", _lua_bind_btn);
	REGISTER_METHOD(L, "bind_axis", _lua_bind_axis);
	REGISTER_METHOD(L, "bind_touch", _lua_bind_tch);
	REGISTER_METHOD(L, "keyboard_model", _lua_set_keyboard_model);
	REGISTER_METHOD(L, "keyboard_layout", _lua_set_keyboard_layout);
	REGISTER_METHOD(L, "keyboard_options", _lua_set_keyboard_options);
	REGISTER_METHOD(L, "repeat_info", _lua_set_repeat_info);
	REGISTER_METHOD(L, "wake", _lua_wake_compositor);
	//backend methods
	REGISTER_METHOD(L, "is_windowed_display", _lua_is_windowed_display);
	REGISTER_METHOD(L, "is_under_x11", _lua_is_under_x11);
	REGISTER_METHOD(L, "is_under_wayland", _lua_is_under_wayland);
	//TODO: other type output
	REGISTER_METHOD(L, "get_window_display", _lua_get_windowed_output);
	//shell methods
	REGISTER_METHOD(L, "shell", _lua_request_shell);
	REGISTER_METHOD(L, "lock_in", _lua_set_lock_timer);
	REGISTER_METHOD(L, "sleep_in", _lua_set_sleep_timer);
	//desktop methods
	REGISTER_METHOD(L, "workspaces", _lua_request_workspaces);
	REGISTER_METHOD(L, "desktop_gaps", _lua_desktop_gap);
	//theme method
	REGISTER_METHOD(L, "read_theme", _lua_read_theme);
	//xwayland
	REGISTER_METHOD(L, "enable_xwayland", _lua_enable_xwayland);
	lua_pop(L, 1); //pop this metatable

	static const struct luaL_Reg lib[] = {
		{"compositor", _lua_get_config},
		{NULL, NULL},
	};

	luaL_newlib(L, lib);
	return 1;
}

static void
_lua_output_created_listener(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct tw_config *config = container_of(listener, struct tw_config,
	                                        output_created_listener);
	(void)output;
	(void)config;
}

static void
_lua_output_destroyed_listener(struct wl_listener *listener, void *data)
{
	struct weston_output *output = data;
	struct tw_config *config = container_of(listener, struct tw_config,
	                                        output_destroyed_listener);
	(void)config;
	(void)output;
}

bool
tw_luaconfig_read(struct tw_config *c, const char *path)
{
	bool safe = true;
	lua_State *L = c->user_data;
	safe = safe && !luaL_loadfile(L, path);
	safe = safe && !lua_pcall(L, 0, 0, 0);
	return safe;
}

char *
tw_luaconfig_read_error(struct tw_config *c)
{
	return strdup(lua_tostring((lua_State *)c->user_data, -1));
}

void
tw_luaconfig_fini(struct tw_config *c)
{
	if (c->user_data)
		lua_close(c->user_data);
}

void
tw_luaconfig_init(struct tw_config *c)
{
	lua_State *L;

	if (c->user_data)
		lua_close(c->user_data);
	if (!(L = luaL_newstate()))
		return;
	luaL_openlibs(L);
	c->user_data = L;

        //REGISTRIES
	lua_pushlightuserdata(L, c); //s1
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_CONFIG); //s0
	lua_pushlightuserdata(L, c->compositor);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_COMPOSITOR); //0
	lua_pushlightuserdata(L, c->config_table);
	lua_setfield(L, LUA_REGISTRYINDEX, CONFIG_TABLE);
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_HINT);

	wl_list_init(&c->output_created_listener.link);
	c->output_created_listener.notify = _lua_output_created_listener;
	wl_signal_add(&c->compositor->output_created_signal,
	              &c->output_created_listener);

        wl_list_init(&c->output_destroyed_listener.link);
	c->output_destroyed_listener.notify = _lua_output_destroyed_listener;
	wl_signal_add(&c->compositor->output_destroyed_signal,
	              &c->output_destroyed_listener);

	// preload the taiwins module
	luaL_requiref(L, "taiwins", luaopen_taiwins, true);
}
