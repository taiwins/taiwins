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

#include <strops.h>
#include "../lua_helper.h"
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

//////////////////////////////////////////////////////////////////
///////////////////// binding functions //////////////////////////
//////////////////////////////////////////////////////////////////

static inline void
_lua_run_binding(void *data)
{
	struct tw_binding *b = data;
	lua_State *L = b->user_data;
	lua_getfield(L, LUA_REGISTRYINDEX, b->name);
	if (lua_pcall(L, 0, 0, 0)) {
		struct tw_config *config = to_user_config(L);
		_lua_error(config, "error calling lua bindings\n");
	}
	lua_settop(L, 0);
}

static void
_lua_run_keybinding(struct weston_keyboard *keyboard, const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_btnbinding(struct weston_pointer *pointer, const struct timespec *time, uint32_t btn,
		    void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_axisbinding(struct weston_pointer *pointer,
		     const struct timespec *time,
		     struct weston_pointer_axis_event *event,
		     void *data)
{
	_lua_run_binding(data);
}


static struct tw_binding *
_new_lua_binding(struct tw_config *config, enum tw_binding_type type)
{
	struct tw_binding *b = vector_newelem(&config->lua_bindings);
	b->user_data = config->L;
	b->type = type;
	sprintf(b->name, "luabinding_%x", config->lua_bindings.len);
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
parse_binding(struct tw_binding *b, const char *seq_string)
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
tw_config_find_binding(struct tw_config *config,
			    const char *name)
{
	for (int i = 0; i < TW_BUILTIN_BINDING_SIZE; i++) {
		if (strcmp(config->builtin_bindings[i].name, name) == 0)
			return &config->builtin_bindings[i];
	}
	return NULL;
}

static inline int
_lua_bind(lua_State *L, enum tw_binding_type binding_type)
{
	//first argument
	struct tw_config *cd = to_user_config(L);
	struct tw_binding *binding_to_find = NULL;
	const char *key = NULL;

	struct tw_binding temp = {0};
	const char *binding_seq = lua_tostring(L, 3);
	temp.type = binding_type;
	if (!binding_seq || !parse_binding(&temp, binding_seq))
		goto err_binding;
	//builtin binding
	if (tw_lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		binding_to_find = tw_config_find_binding(cd, key);
		if (!binding_to_find || binding_to_find->type != binding_type)
			goto err_binding;
	}
	//user binding
	else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
		//create a function in the registry so we can call it later.
		binding_to_find = _new_lua_binding(cd, binding_type);
		lua_pushvalue(L, 2);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding
	} else
		goto err_binding;

	//now we copy the binding seq to
	memcpy(binding_to_find->keypress, temp.keypress, sizeof(temp.keypress));
	return 0;
err_binding:
	cd->quit = true;
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

//////////////////////////////////////////////////////////////////
///////////////////// options functions //////////////////////////
//////////////////////////////////////////////////////////////////

static enum tw_option_type
_lua_type(lua_State *L, int pos)
{
	//here you need to check color first,
	if (tw_lua_is_rgb(L, pos, NULL))
		return TW_OPTION_RGB;
	if (tw_lua_isnumber(L, pos) && lua_gettop(L) == 3)
		return TW_OPTION_INT;
	if (tw_lua_isstring(L, pos) && lua_gettop(L) == 3)
		return TW_OPTION_STR;
	if (lua_isboolean(L, pos) && lua_gettop(L) == 3)
		return TW_OPTION_BOOL;
	return TW_OPTION_INVALID;
}

static inline uint32_t
_lua_torgb(lua_State *L, int pos)
{
	unsigned int r,g,b;
	if (tw_lua_isstring(L, pos))
		sscanf(lua_tostring(L, pos), "#%2x%2x%2x", &r,&g,&b);
	else {
		r = lua_tonumber(L, pos);
		g = lua_tonumber(L, pos + 1);
		b = lua_tonumber(L, pos + 2);
	}
	return (r << 16) + (g << 8) + b;
}

static void
_lua_set_bytype(lua_State *L, int pos, enum tw_option_type type,
	      struct tw_option_listener *listener)
{
	switch (type) {
	case TW_OPTION_RGB:
		listener->arg.u = _lua_torgb(L, pos);
		break;
	case TW_OPTION_INT:
		listener->arg.i = lua_tonumber(L, pos);
		break;
	case TW_OPTION_STR:
		strop_ncpy((char *)listener->arg.s, lua_tostring(L, pos), 128);
		break;
	case TW_OPTION_BOOL:
		listener->arg.u = lua_toboolean(L, pos);
		break;
	default:
		break;
	}
}

static int
_lua_set_value(lua_State *L)
{
	//okay?
	struct tw_config *c = to_user_config(L);
	enum tw_option_type type = _lua_type(L, 3);

	if (!tw_lua_isstring(L, 2) || type == TW_OPTION_INVALID) {
		return luaL_error(L, "invalid arguments\n");
	}
	struct tw_option *opt = NULL;
	vector_for_each(opt, &c->option_hooks) {
		struct tw_option_listener *listener = NULL;
		if (strncmp(opt->key, lua_tostring(L, 2), 32) == 0)
			wl_list_for_each(listener, &opt->listener_list, link) {
				if (listener->type != type)
					goto err;
				_lua_set_bytype(L, 3, type, listener);
				listener->apply(c, listener);
			}
	}
	return 0;
err:
	return luaL_error(L, "invalid type\n");
}

//////////////////////////////////////////////////////////////////
/////////////////////// misc configuration ///////////////////////
//////////////////////////////////////////////////////////////////

static int
_lua_set_keyboard_model(lua_State *L)
{
	struct tw_config *c = to_user_config(L);
	tw_lua_stackcheck(L, 2);
	c->xkb_rules.model = strdup(luaL_checkstring(L, 2));
	return 0;
}

static int
_lua_set_keyboard_layout(lua_State *L)
{
	struct tw_config *c = to_user_config(L);
	tw_lua_stackcheck(L, 2);
	c->xkb_rules.layout = strdup(luaL_checkstring(L, 2));
	return 0;
}

static int
_lua_set_keyboard_options(lua_State *L)
{
	struct tw_config *c = to_user_config(L);
	tw_lua_stackcheck(L, 2);
	c->xkb_rules.options = strdup(luaL_checkstring(L, 2));
	return 0;
}

/* usage: compositor.set_repeat_info(100, 40) */
static int
_lua_set_repeat_info(lua_State *L)
{
	struct tw_config *c = to_user_config(L);
	tw_lua_stackcheck(L, 3);
	int32_t rate = luaL_checknumber(L, 2);
	int32_t delay = luaL_checknumber(L, 3);
	c->kb_delay = delay;
	c->kb_repeat = rate;
	return 0;
}

static int
_lua_get_config(lua_State *L)
{
	//okay, this totally works
	lua_newtable(L);
	luaL_getmetatable(L, "compositor");
	lua_setmetatable(L, -2);
	return 1;
}


void
tw_config_init_luastate(struct tw_config *c)
{
	lua_State *L;
	struct tw_config_component_listener *component;

	if (c->L)
		lua_close(c->L);
	if (!(L = luaL_newstate()))
		return;
	luaL_openlibs(L);
	c->L = L;
	if (c->lua_bindings.elems)
		vector_destroy(&c->lua_bindings);
	vector_init(&c->lua_bindings, sizeof(struct tw_binding), NULL);

	//config userdata
	lua_pushlightuserdata(L, c); //s1
	lua_setfield(L, LUA_REGISTRYINDEX, "__config"); //s0

	//create metatable and the userdata
	luaL_newmetatable(L, "compositor"); //s1
	lua_pushvalue(L, -1); //s2
	lua_setfield(L, -2, "__index"); //s1
	//register all the callbacks
	REGISTER_METHOD(L, "bind_key", _lua_bind_key);
	REGISTER_METHOD(L, "bind_btn", _lua_bind_btn);
	REGISTER_METHOD(L, "bind_axis", _lua_bind_axis);
	REGISTER_METHOD(L, "bind_touch", _lua_bind_tch);
	REGISTER_METHOD(L, "keyboard_model", _lua_set_keyboard_model);
	REGISTER_METHOD(L, "keyboard_layout", _lua_set_keyboard_layout);
	REGISTER_METHOD(L, "keyboard_options", _lua_set_keyboard_options);
	REGISTER_METHOD(L, "repeat_info", _lua_set_repeat_info);
	REGISTER_METHOD(L, "option", _lua_set_value);

	//now adding config components
	wl_list_for_each(component, &c->lua_components, link)
		component->init(c, L, component);

	lua_pushcfunction(L, _lua_get_config);
	lua_setglobal(L, "require_compositor");
	lua_pop(L, 1); //now the stack should be zero

}
