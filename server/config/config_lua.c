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
	lua_getfield(L, LUA_REGISTRYINDEX, b->name);
	if (lua_pcall(L, 0, 0, 0)) {
		struct tw_config *config = to_user_config(L);
		_lua_error(config, "error calling lua bindings\n");
	}
	lua_settop(L, 0);
}

static void
_lua_run_keybinding(struct weston_keyboard *keyboard,
                    const struct timespec *time, uint32_t key,
                    uint32_t option, void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_btnbinding(struct weston_pointer *pointer,
                    const struct timespec *time, uint32_t btn,
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

/*******************************************************************************
 * config option
 ******************************************************************************/

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

/*******************************************************************************
 * main config
 ******************************************************************************/

#define REGISTRY_COMPOSITOR "__compositor"

static bool
_lua_init_backend(struct tw_config *, lua_State *, struct tw_backend *);

static bool
_lua_init_desktop(struct tw_config *, lua_State *, struct desktop *);

static bool
_lua_init_shell(struct tw_config *, lua_State *, struct shell *);

static bool
_lua_init_theme(struct tw_config *, lua_State *, struct theme *);

static bool
_lua_init_xwayland(struct tw_config *, lua_State *, struct tw_xwayland *);

static inline struct weston_compositor *
_lua_to_compositor(lua_State *L)
{
	struct weston_compositor *ec;

	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_COMPOSITOR);
	ec = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return ec;
}

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
	lua_pushlightuserdata(L, c->compositor);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_COMPOSITOR); //0

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

	//init config modules
	_lua_init_backend(c, L, tw_backend_get_global());
	_lua_init_shell(c, L, tw_shell_get_global());
	_lua_init_desktop(c, L, tw_desktop_get_global());
	_lua_init_theme(c, L, tw_theme_get_global());
	_lua_init_xwayland(c, L, tw_xwayland_get_global());

	lua_pushcfunction(L, _lua_get_config);
	lua_setglobal(L, "require_compositor");
	lua_pop(L, 1); //now the stack should be zero

}

/*******************************************************************************
 * backend configs
 ******************************************************************************/

#define METATABLE_OUTPUT "metatable_output"
#define REGISTRY_BACKEND "__backend"

typedef struct { int rotate; bool flip; enum wl_output_transform t;} transform_t;
static transform_t TRANSFORMS[] = {
	{0, false, WL_OUTPUT_TRANSFORM_NORMAL},
	{90, false, WL_OUTPUT_TRANSFORM_90},
	{180, false, WL_OUTPUT_TRANSFORM_180},
	{270, false, WL_OUTPUT_TRANSFORM_270},
	{0, true, WL_OUTPUT_TRANSFORM_FLIPPED},
	{90, true, WL_OUTPUT_TRANSFORM_FLIPPED_90},
	{180, true, WL_OUTPUT_TRANSFORM_FLIPPED_180},
	{270, true, WL_OUTPUT_TRANSFORM_FLIPPED_270},
};

struct _lua_bkend_output {
	struct weston_output *output;
	struct tw_backend_output *bkend_output;
};

static inline struct tw_backend *
_lua_to_backend(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_BACKEND);
	struct tw_backend *b = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return b;
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
	struct tw_backend_output *to = NULL;
	struct weston_output *output;
	struct _lua_bkend_output *lua_output;
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
			return luaL_error(L, "no windowed output available");
	}

	to = tw_backend_output_from_weston_output(output, backend);
	lua_output = lua_newuserdata(L, sizeof(struct _lua_bkend_output));
	lua_output->output = output;
	lua_output->bkend_output = to;
	luaL_getmetatable(L, METATABLE_OUTPUT);
	lua_setmetatable(L, -2);
	return 1;
}

static inline enum wl_output_transform
_lua_output_transfrom_from_value(lua_State *L, int rotate, bool flip)
{
	for (unsigned i = 0; i < NUMOF(TRANSFORMS); i++)
		if (TRANSFORMS[i].rotate == rotate && TRANSFORMS[i].flip == flip)
			return TRANSFORMS[i].t;
	return luaL_error(L, "invalid transforms option.");
}

static int
_lua_output_rotate_flip(lua_State *L)
{
	struct _lua_bkend_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);

	if (lua_gettop(L) == 1) {
		transform_t transform =
			TRANSFORMS[lua_output->output->transform];
		lua_pushinteger(L, transform.rotate);
		lua_pushboolean(L, transform.flip);
		return 2;
	} else if(lua_gettop(L) == 2) {
		int rotate = luaL_checkinteger(L, 2);
		bool flip = false;
		tw_backend_output_set_transform(
			lua_output->bkend_output,
			_lua_output_transfrom_from_value(L, rotate, flip));
		return 0;
	} else if (lua_gettop(L) == 3) {
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		int rotate = luaL_checkinteger(L, 2);
		int flip = lua_toboolean(L, 3);
		tw_backend_output_set_transform(
			lua_output->bkend_output,
			_lua_output_transfrom_from_value(L, rotate, flip));
		return 0;
	} else
		return luaL_error(L, "invalid number of arguments");
}

static int
_lua_output_scale(lua_State *L)
{
	struct _lua_bkend_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, lua_output->output->scale);
		return 1;
	} else {
		tw_lua_stackcheck(L, 2);
		unsigned scale = luaL_checkinteger(L, 2);
		if (scale <= 0 || scale > 4)
			return luaL_error(L, "invalid display scale");
		tw_backend_output_set_scale(lua_output->bkend_output,
		                                    scale);
		return 0;
	}
}

static int
_lua_output_resolution(lua_State *L)
{
	struct _lua_bkend_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, lua_output->output->width);
		lua_pushinteger(L, lua_output->output->height);
		return 2;
	} else {
		//TODO we deal with THIS later
		tw_lua_stackcheck(L, 2);
		return 0;
	}
}

static int
_lua_output_position(lua_State *L)
{
	struct _lua_bkend_output *output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, output->output->x);
		lua_pushinteger(L, output->output->y);
		return 2;
	} else {
		//TODO we deal with this later.
		tw_lua_stackcheck(L, 2);
		return 0;
	}
}

static bool
_lua_init_backend(struct tw_config *c, lua_State *L,
                  struct tw_backend *b)
{
	lua_pushlightuserdata(L, b);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_BACKEND);
	//metatable for output
	luaL_newmetatable(L, METATABLE_OUTPUT);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__newindex");
	//here we choose to make into functions so use output:flip(270) instead
	//of output.flip = 270
	REGISTER_METHOD(L, "rotate_flip", _lua_output_rotate_flip);
	REGISTER_METHOD(L, "scale", _lua_output_scale);
	REGISTER_METHOD(L, "resolution", _lua_output_resolution);
	REGISTER_METHOD(L, "position", _lua_output_position);
	lua_pop(L, 1);

	//global methods
	REGISTER_METHOD(L, "is_windowed_display", _lua_is_windowed_display);
	REGISTER_METHOD(L, "is_under_x11", _lua_is_under_x11);
	REGISTER_METHOD(L, "is_under_wayland", _lua_is_under_wayland);

	//TODO for now we now allows creating windowed output for user to
	//configure
	REGISTER_METHOD(L, "get_window_display", _lua_get_windowed_output);

	return true;
}

/*******************************************************************
 * shell config
 ******************************************************************/

#define METATABLE_SHELL "metatable_shell"
#define REGISTRY_SHELL "__shell"

static struct shell *
_lua_to_shell(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);
	struct shell *sh = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sh;
}

static int
_lua_set_wallpaper(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	tw_lua_stackcheck(L, 2);
	const char *path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "wallpaper does not exist!");
	tw_shell_set_wallpaper(shell, path);
	return 0;
}

static int
_lua_set_widgets(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	tw_lua_stackcheck(L, 2);
	const char *path = luaL_checkstring(L, 2);
	if (!is_file_exist(path))
		return luaL_error(L, "widget path does not exist!");
	tw_shell_set_widget_path(shell, path);
	return 0;
}

static int
_lua_set_panel_position(lua_State *L)
{
	struct shell *shell = _lua_to_shell(L);
	luaL_checktype(L, 2, LUA_TSTRING);
	const char *pos = lua_tostring(L, 2);
	if (strcmp(pos, "bottom") == 0)
		tw_shell_set_panel_pos(shell, TAIWINS_SHELL_PANEL_POS_BOTTOM);
	else if (strcmp(pos, "top") == 0)
		tw_shell_set_panel_pos(shell, TAIWINS_SHELL_PANEL_POS_TOP);
	else
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
	struct shell *shell = _lua_to_shell(L);
	vector_t menu;

	vector_init_zero(&menu, sizeof(struct tw_menu_item), NULL);
	tw_lua_stackcheck(L, 2);
	luaL_checktype(L, 2, LUA_TTABLE);
	if (!_lua_parse_menu(L, &menu)) {
		vector_destroy(&menu);
		return luaL_error(L, "error parsing menus.");
	}
	tw_shell_set_menu(shell, &menu);
	return 0;
}

static int
_lua_set_sleep_timer(lua_State *L)
{
	return 0;
}

static int
_lua_set_lock_timer(lua_State *L)
{
	struct weston_compositor *ec = _lua_to_compositor(L);
	tw_lua_stackcheck(L, 2);
	int32_t seconds = luaL_checknumber(L, 2);
	if (seconds < 0) {
		return luaL_error(L, "idle time must be a non negative integers.");
	}
	//TODO verify this
	ec->idle_time = seconds;
	//shell->lock_countdown = seconds;
	return 0;
}

static int
_lua_request_shell(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, METATABLE_SHELL);
	lua_setmetatable(L, -2);
	return 1;
}

static bool
_lua_init_shell(struct tw_config *c, lua_State *L, struct shell *shell)
{
	lua_pushlightuserdata(L, shell);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);
	//shell methods
	luaL_newmetatable(L, METATABLE_SHELL);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	REGISTER_METHOD(L, "set_wallpaper", _lua_set_wallpaper);
	REGISTER_METHOD(L, "init_widgets", _lua_set_widgets);
	REGISTER_METHOD(L, "panel_position", _lua_set_panel_position);
	REGISTER_METHOD(L, "set_menus", _lua_set_menus);
	//global methods
	lua_pop(L, 1);
	REGISTER_METHOD(L, "shell", _lua_request_shell);
	REGISTER_METHOD(L, "lock_in", _lua_set_lock_timer);
	REGISTER_METHOD(L, "sleep_in", _lua_set_sleep_timer);

	return true;
}

/****************************************************************************
 * desktop config
 ***************************************************************************/

#define METATABLE_WORKSPACE "metatable_workspace"
#define METATABLE_DESKTOP "metatable_desktop"
#define REGISTRY_DESKTOP "__desktop"

static inline struct desktop*
_lua_to_desktop(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_DESKTOP);
	struct desktop *d = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return d;
}

static int
_lua_request_workspaces(lua_State *L)
{
	struct desktop *d = _lua_to_desktop(L);
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
	return 1;
}

static int
_lua_set_ws_layout(lua_State *L)
{
	int index;
	struct desktop *d = _lua_to_desktop(L);
	luaL_checktype(L, 2, LUA_TSTRING);
	const char *layout = lua_tostring(L, 2);

	lua_pushstring(L, "index");
	lua_gettable(L, 1);
	index = lua_tonumber(L, -1);

	if (!tw_desktop_set_workspace_layout(d, index, layout))
		return luaL_error(L, "invalid layout type %s\n", layout);

	lua_pop(L, 1);
	return 0;
}

static int
_lua_desktop_gap(lua_State *L)
{
	int inner, outer;
	struct desktop *d = _lua_to_desktop(L);
	//get gaps
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
		tw_desktop_set_gap(d, inner, outer);
		return 0;
	}
	return luaL_error(L, "invalid size of params for gap.");
}

static int
_lua_request_desktop(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, METATABLE_DESKTOP);
	lua_setmetatable(L, -2);
	return 1;
}

/*
 * exposed lua functions
 *
 * desktop global: setting gap; get_workspaces;
 *
 * workspace: switch layouts?
 */
static bool
_lua_init_desktop(struct tw_config *c, lua_State *L, struct desktop *d)
{
	lua_pushlightuserdata(L, d); //s1
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_DESKTOP); //s0

	//metatable for desktop API
	luaL_newmetatable(L, METATABLE_DESKTOP);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	REGISTER_METHOD(L, "workspaces", _lua_request_workspaces);
	REGISTER_METHOD(L, "gaps", _lua_desktop_gap);
	lua_pop(L, 1);

	//metatable for workspace
	luaL_newmetatable(L, METATABLE_WORKSPACE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	REGISTER_METHOD(L, "set_layout", _lua_set_ws_layout);
	lua_pop(L, 1);

	REGISTER_METHOD(L, "desktop", _lua_request_desktop);

	return true;
}

/*******************************************************************************
 * tw_theme
 ******************************************************************************/
extern int tw_theme_read(lua_State *L);

static bool
_lua_init_theme(struct tw_config *config, lua_State *L, struct theme *theme)
{
	lua_pushlightuserdata(L, tw_theme_access_theme(theme));
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme");
	REGISTER_METHOD(L, "read_theme", tw_theme_read);

	return true;
}

/*******************************************************************************
 * tw_xwayland
 ******************************************************************************/
static inline struct tw_xwayland *
_lua_to_xwayland(lua_State *L)
{
	struct tw_xwayland *data;

	lua_getfield(L, LUA_REGISTRYINDEX, "tw_xwayland");
	data = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return data;
}

static int
_lua_enable_xwayland(lua_State *L)
{
	bool val;
	struct tw_xwayland *xwayland = _lua_to_xwayland(L);
	tw_lua_assert(L, lua_gettop(L) == 2,
	              "xwayland: invalid number of arguments");
	val = lua_toboolean(L, 2);
	tw_xwayland_enable(xwayland, val);
	return 0;
}

static bool
_lua_init_xwayland(struct tw_config *config, lua_State *L,
                   struct tw_xwayland *xwayland)
{
	lua_pushlightuserdata(L, tw_xwayland_get_global());
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_xwayland");
	REGISTER_METHOD(L, "enable_xwayland", _lua_enable_xwayland);
	return true;
}
