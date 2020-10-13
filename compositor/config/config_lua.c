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
#include <wayland-server.h>
#include <wayland-util.h>

#include <ctypes/strops.h>
#include <ctypes/os/file.h>
#include <ctypes/vector.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <twclient/theme.h>

#include <taiwins/xdg.h>

#include "lua_helper.h"
#include "bindings.h"
#include "config_internal.h"

static inline struct tw_config *
to_user_config(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__config");
	struct tw_config *c = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return c;
}

/******************************************************************************
 * binding functions
 *****************************************************************************/

static inline void
_lua_run_binding(void *data)
{
	struct tw_binding *b = data;
	lua_State *L = b->user_data;

	lua_getfield(L, LUA_REGISTRYINDEX, b->name);
	if (lua_pcall(L, 0, 0, 0)) {
		tw_logl("error calling lua bindings\n");
	}
	lua_settop(L, 0);
}

static bool
_lua_run_keybinding(struct tw_keyboard *keyboard,
                    uint32_t time, uint32_t key, uint32_t mods,
                    uint32_t option, void *data)
{
	_lua_run_binding(data);
	return true;
}

static bool
_lua_run_btnbinding(struct tw_pointer *pointer,
                    uint32_t time, uint32_t btn, uint32_t mods, void *data)
{
	_lua_run_binding(data);
	return true;
}

static bool
_lua_run_axisbinding(struct tw_pointer *pointer, uint32_t time,
                     double delta, enum wl_pointer_axis direction,
                     uint32_t mods, void *data)
{
	_lua_run_binding(data);
	return true;
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

 /*****************************************************************************
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

#define REGISTRY_CONFIG "__config"
#define REGISTRY_WORKSPACES "__workspaces"
#define REGISTRY_HINT "__hint"
#define CONFIG_TABLE "__config_table"

#define METATABLE_COMPOSITOR "metatable_compositor"
#define METATABLE_WORKSPACE "metatable_workspace"

static inline struct tw_config_table *
_lua_to_config_table(lua_State *L)
{
	struct tw_config_table *table;

	lua_getfield(L, LUA_REGISTRYINDEX, CONFIG_TABLE);
	table = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return table;
}

extern int tw_theme_read(lua_State *L, struct tw_theme *theme);

/******************************************************************************
 * output configs
 *****************************************************************************/

/**
 * @brief request a output from global output. Push a table on stack if the
 * output exists. Otherwise stack unchanged.
 *
 * TODO : I do not know what is use of this
 * [-0, +(1|0), -]
 */

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
_lua_read_display_rotate_flip(lua_State *L, struct tw_config_table *t, int idx)
{
	int rotate = 0;
	bool dirty, flip = false;
	tw_config_transform_t transform;

	lua_getfield(L, 3, "rotation");
	if (tw_lua_isnumber(L, -1)) {
		rotate = lua_tonumber(L, -1);
		dirty = true;
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
	} else
		return luaL_error(L, "config_display, invalid rotation");

	lua_getfield(L, 3, "flip");
	if (lua_isboolean(L, -1)) {
		flip = lua_toboolean(L, -1);
		dirty = true;
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
	} else
		return luaL_error(L, "config_display, invalid flip");

	if (dirty) {
		transform.t = _lua_output_transfrom_from_value(L, rotate, flip);
		SET_PENDING(&t->outputs[idx].transform,
		            transform, transform.t);
		tw_config_table_dirty(t, dirty);
	}
	return 0;
}

static int
_lua_read_display_scale(lua_State *L, struct tw_config_table *t, int idx)
{
	unsigned int scale = 1;

	lua_getfield(L, 3, "scale");
	if (tw_lua_isnumber(L, -1)) {
		scale = lua_tonumber(L, -1);
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	} else
		return luaL_error(L, "config_display: scale error");
	if (scale >= 4)
		return luaL_error(L, "config_display: invalid scale %d for "
		                  "display %s", scale, t->outputs[idx].name);

        SET_PENDING(&(t->outputs[idx].scale), val, scale);
	tw_config_table_dirty(t, true);

	return 0;
}

static int
_lua_read_display_mode(lua_State *L, struct tw_config_table *t, int idx)
{
	int w, h;
	lua_getfield(L, 3, "mode");
	if (tw_lua_is_tuple2(L, -1, &w, &h) ||
	    tw_lua_is_int2str(L, -1, &w, &h)) {
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	} else
		return luaL_error(L, "config_display: mode error");
	if (w <= 0 || h <= 0 || w > 10000 || h > 10000)
		return luaL_error(L, "config_display: invalid mode (%d,%d)",
		                  w, h);
	SET_PENDING(&t->outputs[idx].width, uval, w);
	SET_PENDING(&t->outputs[idx].height, uval, h);
	tw_config_table_dirty(t, true);

	return 0;
}

static int
_lua_read_display_position(lua_State *L, struct tw_config_table *t, int idx)
{
	int x, y;
	lua_getfield(L, 3, "position");
	if (tw_lua_is_tuple2(L, -1, &x, &y) ||
	    tw_lua_is_int2str(L, -1, &x, &y)) {
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	if (x > 10000 || y > 10000 || x < -10000 || y < -10000)
		return luaL_error(L, "config_display: invalid display ",
		                  "position (%d,%d)", x, y);
	SET_PENDING(&t->outputs[idx].posx, val, x);
	SET_PENDING(&t->outputs[idx].posy, val, y);
	tw_config_table_dirty(t, true);

	return 0;
}

static int
_lua_read_display_enable(lua_State *L, struct tw_config_table *t, int idx)
{
	bool enabled = true;
	lua_getfield(L, 3, "enable");
	if (lua_isboolean(L, -1)) {
		enabled = lua_toboolean(L, -1);
		lua_pop(L, 1);
	} else if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	SET_PENDING(&t->outputs[idx].enabled, enable, enabled);
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_read_display(lua_State *L, struct tw_config_table *t, uint32_t idx)
{
	_lua_read_display_enable(L, t, idx);
	_lua_read_display_scale(L, t, idx);
	_lua_read_display_position(L, t, idx);
	_lua_read_display_mode(L, t, idx);
	_lua_read_display_rotate_flip(L, t, idx);
	return 0;
}

static int
_lua_config_display(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
	const char *name;

	if (lua_gettop(L) != 3)
		return luaL_error(L, "config_display: invalid number of args");
	if (!tw_lua_isstring(L, 2))
		return luaL_error(L, "config_display: expecting a string");
	if (!lua_istable(L, 3))
		return luaL_error(L, "config_display: expecting a table");

        name = lua_tostring(L, 2);
	for (unsigned i = 0; i < NUMOF(t->outputs); i++) {
		if (!strcmp(name, t->outputs[i].name)) {
			//an old output.
			return _lua_read_display(L, t, i);
		} else if (strlen(t->outputs[i].name) == 0) {
			//or a new one.
			strncpy(t->outputs[i].name, name, 31);
			return _lua_read_display(L, t, i);
		}
	}
	return luaL_error(L, "config_display: too many output configs");
}

/******************************************************************************
 * workspaces config.
 *****************************************************************************/

static int
_lua_set_ws_layout(lua_State *L)
{
	int index, layout_type;
	const char *layout;
	struct tw_config_table *table = _lua_to_config_table(L);

	if (!tw_lua_istable(L, 1, METATABLE_WORKSPACE))
		return luaL_error(L, "%s: invalid workspace\n",
		                  "workspace.set_layout");
	lua_getfield(L, 1, "index");
	index = lua_tonumber(L, -1);
	lua_pop(L, 1);
	luaL_checktype(L, 2, LUA_TSTRING);
	layout = lua_tostring(L, 2);
	if (index < 0 || index >= MAX_WORKSPACES)
		return luaL_error(L, "%s: invaild workpsace\n",
		                  "workspace.set_layout");

	layout_type = tw_xdg_layout_type_from_name(layout);
	if (layout_type >= LAYOUT_FLOATING && layout_type <= LAYOUT_FULLSCREEN)
		SET_PENDING(&table->workspaces[index].layout,
		            layout, layout_type);
	else
		return luaL_error(L, "%s: invaild layout\n",
		                  "workspace.set_layout");
	tw_config_table_dirty(table, true);
	return 0;
}

static int
_lua_request_workspaces(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WORKSPACES);
	if (lua_istable(L, -1))
		return 1;
	//create workspaces if not created
	lua_pop(L, 1);
	lua_newtable(L); //1
	for (int i = 0; i < MAX_WORKSPACES ; i++) {

		lua_newtable(L); //2
		luaL_getmetatable(L, METATABLE_WORKSPACE); //3
		lua_setmetatable(L, -2); //2

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
 * enables
 *****************************************************************************/

static int
_lua_enable_object(lua_State *L, char *name,
                   enum tw_config_enable_global global)
{
	bool val;
	struct tw_config_table *t =
		_lua_to_config_table(L);

	tw_lua_assert(L, lua_gettop(L) == 2,
	              "%s: invalid number of arguments", name);
	val = lua_toboolean(L, 2);
	if (val)
		t->enable_globals |= global;
	else
		t->enable_globals &= ~global;
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_enable_xwayland(lua_State *L)
{
	return _lua_enable_object(L, "xwayland", TW_CONFIG_GLOBAL_XWAYLAND);
}

static int
_lua_enable_desktop(lua_State *L)
{
	return _lua_enable_object(L, "desktop", TW_CONFIG_GLOBAL_DESKTOP);
}

static int
_lua_enable_bus(lua_State *L)
{
	return _lua_enable_object(L, "dbus", TW_CONFIG_GLOBAL_BUS);
}

static int
_lua_enable_taiwins_shell(lua_State *L)
{
	return _lua_enable_object(L, "taiwins_shell",
	                          TW_CONFIG_GLOBAL_TAIWINS_SHELL);
}

static int
_lua_enable_layer_shell(lua_State *L)
{
	return _lua_enable_object(L, "layer_shell",
	                          TW_CONFIG_GLOBAL_LAYER_SHELL);
}

static int
_lua_enable_taiwins_console(lua_State *L)
{
	return _lua_enable_object(L, "taiwins_console",
	                          TW_CONFIG_GLOBAL_TAIWINS_CONSOLE);
}

static int
_lua_enable_taiwins_theme(lua_State *L)
{
	return _lua_enable_object(L, "taiwins_theme",
	                          TW_CONFIG_GLOBAL_TAIWINS_THEME);
}

/******************************************************************************
 * global config
 *****************************************************************************/

static int
_lua_set_keyboard_model(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

	tw_lua_stackcheck(L, 2);
	t->xkb_rules->model = strdup(luaL_checkstring(L, 2));
	return 0;
}

static int
_lua_set_keyboard_layout(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

	tw_lua_stackcheck(L, 2);
	t->xkb_rules->layout = strdup(luaL_checkstring(L, 2));
	tw_config_table_dirty(t, true);
	return 0;
}

static int
_lua_set_keyboard_options(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);

	tw_lua_stackcheck(L, 2);
	t->xkb_rules->options = strdup(luaL_checkstring(L, 2));
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
_lua_read_theme(lua_State *L)
{
	struct tw_config_table *table = _lua_to_config_table(L);
	struct tw_theme *theme;

	if (!(table->enable_globals & TW_CONFIG_GLOBAL_TAIWINS_THEME))
		return luaL_error(L, "read_theme: theme option not enabled");
	//required for tw_theme_read
	theme = calloc(1, sizeof(struct tw_theme));
	if (!theme)
		return luaL_error(L, "read_theme: failed allocation");

	tw_theme_read(L, theme);
	SET_PENDING(&table->theme, theme, theme);
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
_lua_set_panel_position(lua_State *L)
{
	struct tw_config_table *t = _lua_to_config_table(L);
	const char *pos;

        luaL_checktype(L, 1, LUA_TSTRING);
	pos = lua_tostring(L, 1);
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
_lua_desktop_gap(lua_State *L)
{
	int inner, outer;
	struct tw_config_table *t = _lua_to_config_table(L);
	if (lua_gettop(L) == 3) {
		inner = luaL_checkinteger(L, 2);
		outer = luaL_checkinteger(L, 3);
		if (inner < 0 || inner > 100 ||
		    outer < 0 || outer > 100)
			return luaL_error(L, "invalid size of gaps.");
		SET_PENDING(&t->desktop_igap, uval, inner);
		SET_PENDING(&t->desktop_ogap, uval, outer);
		tw_config_table_dirty(t, true);
		return 0;
	}
	return luaL_error(L, "invalid size of params for gap.");
}


static int
luaopen_taiwins(lua_State *L)
{
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
	//objects
	REGISTER_METHOD(L, "enable_xwayland", _lua_enable_xwayland);
	REGISTER_METHOD(L, "enable_bus", _lua_enable_bus);
	REGISTER_METHOD(L, "enable_shell", _lua_enable_taiwins_shell);
	REGISTER_METHOD(L, "enable_console", _lua_enable_taiwins_console);
	REGISTER_METHOD(L, "enable_theme", _lua_enable_taiwins_theme);
	REGISTER_METHOD(L, "enable_layer_shell", _lua_enable_layer_shell);
	REGISTER_METHOD(L, "enable_desktop", _lua_enable_desktop);
	//shell methods
	REGISTER_METHOD(L, "lock_in", _lua_set_lock_timer);
	REGISTER_METHOD(L, "sleep_in", _lua_set_sleep_timer);
	REGISTER_METHOD(L, "panel_pos", _lua_set_panel_position);
	REGISTER_METHOD(L, "set_gaps", _lua_desktop_gap);
	REGISTER_METHOD(L, "workspaces", _lua_request_workspaces);
	//theme method
	REGISTER_METHOD(L, "read_theme", _lua_read_theme);
	REGISTER_METHOD(L, "config_display", _lua_config_display);
	//TODO: config_seat?

	lua_pop(L, 1); //pop this metatable

	static const struct luaL_Reg lib[] = {
		{"compositor", _lua_get_config},
		{NULL, NULL},
	};

	luaL_newlib(L, lib);
	return 1;
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
	lua_pushlightuserdata(L, &c->config_table);
	lua_setfield(L, LUA_REGISTRYINDEX, CONFIG_TABLE);
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_HINT);
	// preload the taiwins module
	luaL_requiref(L, "taiwins", luaopen_taiwins, true);
}
