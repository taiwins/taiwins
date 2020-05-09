/*
 * shell_lua.c - taiwins client shell lua config implementation
 *
 * Copyright (c) 2020 Xichen Zhou
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
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <os/file.h>
#include <strops.h>
#include <client.h>
#include <nk_backends.h>
#include <vector.h>
#include <ui.h>

#include <nuklear_love.h>
#include <widget/widget.h>
#include "helpers.h"
#include "shell.h"

/*******************************************************************************
 * lua helpers
 ******************************************************************************/
#define REGISTRY_SHELL "_shell"
#define REGISTRY_WALLPAPER "_wallpaper"
#define REGISTRY_WIDGETS "_widgets"
#define REGISTRY_CALLBACKS "_widgets_callback"

#define METATABLE_WIDGET "metatable_widget"
#define ANCHOR_CB_FORMAT "_anchor_cb_%.2d"
#define WIDGET_CB_FORMAT "_widget_cb_%.2d"
#define WIDGET_ANCHOR "brief"
#define WIDGET_DRAW "draw"
#define	WIDGET_FILE "file_watch"
#define WIDGET_DEV "device_watch"
#define WIDGET_TIMER "timer"
#define WIDGET_WIDTH "width"
#define WIDGET_HEIGHT "height"

struct shell_widget_runtime {
	struct shell_widget widget;
	lua_State *L;
	struct nk_love_context *runtime;

	int index;
	char widgetcb[32];
	char anchorcb[32];
};

static inline bool
lua_isluafunction(lua_State *L, int pos)
{
	return lua_isfunction(L, pos) && !lua_iscfunction(L, pos);
}

static inline bool
lua_toluafunction(lua_State *L, int pos)
{
	return lua_isluafunction(L, pos);
}

#define _LUA_REGISTER(name, func)                                       \
	lua_pushcfunction(L, func); \
	lua_setfield(L, -2, name)

#define _LUA_ISFIELD(L, type, pos, name)                                \
	({ \
		bool ret = false; \
		lua_getfield(L, pos, name); \
		ret = lua_is##type(L, -1); \
		lua_pop(L, 1); \
		ret; \
	})

#define _LUA_GET_TABLE(L, type, pos, name)                              \
	({ \
		lua_pushstring(L, name); \
		lua_rawget(L, pos); \
		lua_to##type(L, -1); \
	})

static inline bool
_lua_istable(lua_State *L, int pos, const char *type)
{
	bool same = false;
	if (!lua_istable(L, pos))
		return false;
	if (lua_getmetatable(L, pos) != 0) { //+1
		luaL_getmetatable(L, type); //+2
		same = lua_compare(L, -1, -2, LUA_OPEQ);
		lua_pop(L, 2);
	}
	return same;
}

static inline void *
_lua_isudata(lua_State *L, int pos, const char *type)
{
	bool same = false;
	if (!lua_isuserdata(L, pos))
		return NULL;
	if (lua_getmetatable(L, pos) != 0) {
		luaL_getmetatable(L, type);
		same = lua_compare(L, -1, -2, LUA_OPEQ);
		lua_pop(L, 2);
	}
	return same ? lua_touserdata(L, pos) : NULL;
}

/*******************************************************************************
 * lua functions
 ******************************************************************************/

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
	//this would not be the right way to do it though.
	if (lua_gettop(L) == 1 && lua_isstring(L, 1) &&
	    is_file_exist(lua_tostring(L, 1)) &&
	    is_file_type(lua_tostring(L, 1), ".png")) {
		shell_load_wallpaper(shell, lua_tostring(L, 1));
		lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_WALLPAPER);
	} else {
		shell_notif_msg(shell, 128,
		                "set_wallpaper: invalid argument 1 %s\n",
		                lua_tostring(L, 1));
	}
	return 0;
}

static int
_lua_set_panel_position(lua_State *L)
{
	struct desktop_shell *shell = _lua_to_shell(L);
	const char *pos;

	//ensentially you need to tell the server about the location
        luaL_checktype(L, 2, LUA_TSTRING);
	pos = lua_tostring(L, 2);
	if (strcmp(pos, "bottom") == 0) {
		shell->panel_pos = TAIWINS_SHELL_PANEL_POS_BOTTOM;
	} else if (strcmp(pos, "top") == 0) {
		shell->panel_pos = TAIWINS_SHELL_PANEL_POS_TOP;
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

/**
 * @brief parse_menu from -1
 */
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

static struct tw_menu_item shell_default_menu[] = {
	{
		.endnode.title = "Application",
		.has_submenu = true,
		.len = 0,
	},
	{
		.endnode.title = "System",
		.has_submenu = true,
		.len = 1,
	},
	{
		.endnode.title = "Reconfigure",
		.has_submenu = false,
	},
};

static int
_lua_set_menu(lua_State *L)
{
	struct desktop_shell *shell = _lua_to_shell(L);
	vector_t menu;
	vector_t default_menu = {
		.elems = shell_default_menu,
		.len = 3,
		.alloc_len= 3,
		.elemsize = sizeof(struct tw_menu_item),
		.free = dummy_free,
	};

	vector_init_zero(&menu, sizeof(struct tw_menu_item), NULL);
	luaL_checktype(L, 1, LUA_TTABLE);
	if (!_lua_parse_menu(L, &menu)) {
		vector_destroy(&menu);
		return luaL_error(L, "shell.set_menu: %s\n",
		                  "error in parsing menu");
		vector_copy(&shell->menu, &default_menu);
	} else
		shell->menu = menu;
	return 0;
}

/******************************************************************************
 * lua widgets registry
 *****************************************************************************/
static inline int
_lua_n_widgets(lua_State *L)
{
	int n = 0;
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
	n = lua_rawlen(L, -1);
	lua_pop(L, 1);
	return n;
}

static inline void
_lua_set_widget_func(lua_State *L, int pos, const char *type)
{
	if (pos < 0)
		pos = lua_gettop(L) + pos + 1;
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CALLBACKS);
	lua_pushvalue(L, pos);
	lua_setfield(L, -2, type);
	lua_pop(L, 1);
}

static inline void
_lua_get_widget_func(lua_State *L, const char *type)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_CALLBACKS);
	lua_getfield(L, -1, type);
	lua_remove(L, lua_gettop(L)-1);
}

static inline void
_lua_get_widget_userdata(lua_State *L, struct shell_widget_runtime *runtime)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
	lua_rawgeti(L, -1, runtime->index+1);
	lua_remove(L, lua_gettop(L)-1);
}

static int
_lua_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	struct shell_widget_runtime *lua_runtime =
		container_of(widget, struct shell_widget_runtime, widget);
	const char *encoded;
	lua_State *L = lua_runtime->L;
	_lua_get_widget_func(L, lua_runtime->anchorcb);
	_lua_get_widget_userdata(L, lua_runtime);
	//setup context
	lua_pcall(L, 1, 1, 0);
	encoded = lua_tostring(L, -1);
	strcpy(label->label, encoded);
	lua_pop(L, 1);
	return strlen(encoded);
}

static void
_lua_widget_cb(struct nk_context *ctx,
               UNUSED_ARG(float width), UNUSED_ARG(float height),
	       struct tw_appsurf *app)
{
	//get widget
	struct shell_widget *widget =
		container_of(app, struct shell_widget, widget);
	struct shell_widget_runtime *lua_runtime =
		container_of(widget, struct shell_widget_runtime, widget);
	lua_State *L = lua_runtime->L;

	lua_runtime->runtime = nk_love_get_ui(L);
	lua_runtime->runtime->nkctx = ctx;
	//fuction and args
	_lua_get_widget_func(L, lua_runtime->widgetcb);
	nk_love_getfield_ui(L); //1st arg: ui.
	_lua_get_widget_userdata(L, lua_runtime); //2nd arg: widget

	if (lua_pcall(L, 2, 0, 0)) {
		//if errors occured, we draw this instead
		const char *error = lua_tostring(L, -1);
		nk_clear(ctx);
		nk_layout_row_dynamic(ctx, 20, 1);
		nk_text_colored(ctx, error, strlen(error), NK_TEXT_CENTERED,
				nk_rgb(255, 0, 0));
		lua_pop(L, 1);
	}
}

static void
_lua_init_widget_runtime(struct shell_widget_runtime *runtime,
                         lua_State *L, int index)
{
	memset(runtime, 0, sizeof(struct shell_widget_runtime));
	struct shell_widget *widget = &runtime->widget;
	luaL_getmetatable(L, METATABLE_WIDGET);
	lua_setmetatable(L, -2);
	//initialize widget
	widget->ancre_cb = _lua_widget_anchor;
	widget->draw_cb = _lua_widget_cb;
	widget->file_path = NULL;
	widget->subsystem = NULL;
	widget->devname = NULL;
	widget->interval = (struct itimerspec){0};
	widget->user_data = runtime;
	//initialize runtime
	runtime->L = L;
	runtime->index = index;
	sprintf(runtime->anchorcb, ANCHOR_CB_FORMAT, index);
	sprintf(runtime->widgetcb, WIDGET_CB_FORMAT, index);
}

static int
_lua_add_widget_data(lua_State *L, struct shell_widget_runtime *runtime)
{
	struct shell_widget *widget = &runtime->widget;

	///////// brief
	if (!_LUA_ISFIELD(L, luafunction, 1, WIDGET_ANCHOR))
		return luaL_error(L, "widget without %s",
		                  WIDGET_ANCHOR);
	_LUA_GET_TABLE(L, luafunction, 1, WIDGET_ANCHOR);
	_lua_set_widget_func(L, -1, runtime->anchorcb);
	lua_pop(L, 1);

	///////// draw call
	if (!_LUA_ISFIELD(L, luafunction, 1, WIDGET_DRAW))
		widget->draw_cb = NULL;
	else {
		_LUA_GET_TABLE(L, luafunction, 1, WIDGET_DRAW);
		_lua_set_widget_func(L, -1, runtime->widgetcb);
		lua_pop(L, 1);
	}

	///////// width
        if (!_LUA_ISFIELD(L, integer, 1, WIDGET_WIDTH))
		return luaL_error(L, "widget without %s",
		                  WIDGET_WIDTH);
        widget->w = _LUA_GET_TABLE(L, integer, 1, WIDGET_WIDTH);
        lua_pop(L, 1);

	///////// height
        if (!_LUA_ISFIELD(L, integer, 1, WIDGET_HEIGHT))
		return luaL_error(L, "widget without %s",
		                  WIDGET_HEIGHT);
        widget->h = _LUA_GET_TABLE(L, integer, 1, WIDGET_HEIGHT);
	lua_pop(L, 1);
	return 0;
}

static int
_lua_add_widget_watchers(lua_State *L, struct shell_widget_runtime *runtime)
{
	bool registered = false;
	struct shell_widget *widget = &runtime->widget;
	//watchers
	if (_LUA_ISFIELD(L, string, 1, WIDGET_FILE) && !registered) {
		widget->file_path =
			strdup(_LUA_GET_TABLE(L, string, 1, WIDGET_FILE));
		lua_pop(L, 1);
		registered = true;
	} else if (_LUA_ISFIELD(L, integer, 1, WIDGET_TIMER) && !registered) {
		lua_Integer time =
			_LUA_GET_TABLE(L, integer, 1, WIDGET_TIMER);
		widget->interval = (struct itimerspec){
			.it_value = {time, 0},
			.it_interval = {time, 0},
		};
		lua_pop(L, 1);
		registered = true;
	} else if (_LUA_ISFIELD(L, string, 1, WIDGET_DEV) && !registered) {
		widget->subsystem =
			strdup(_LUA_GET_TABLE(L, string, 1, WIDGET_DEV));
		lua_pop(L, 1);
		registered = true;
	}
	return 0;
}

static int
_lua_new_widget_from_table(lua_State *L)
{
	struct shell_widget_runtime *runtime;
	int n_widgets = _lua_n_widgets(L);

	if (lua_gettop(L) != 1 || !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "shell.new_widget:invalid arguments\n");
	//init runtime
	runtime = lua_newuserdata(L, sizeof(struct shell_widget_runtime));
	if (!runtime)
		return luaL_error(L, "uneable to create the widget.");
	//set the metatable for here
	luaL_getmetatable(L, METATABLE_WIDGET);
	lua_setmetatable(L, -2);

	_lua_init_widget_runtime(runtime, L, (int)n_widgets);
	_lua_add_widget_data(L, runtime);
	_lua_add_widget_watchers(L, runtime);

	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
	lua_pushvalue(L, 2);
	lua_rawseti(L, -2, n_widgets+1);
	lua_pop(L, 1);

	return 0;
}

static int
_lua_widget_done(lua_State *L)
{
	return _lua_new_widget_from_table(L);
}

static int
_lua_widget_set_anchor(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isluafunction(L, 2))
		return luaL_error(L, "invalid argument");
	if ( !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");

	lua_setfield(L, 1, WIDGET_ANCHOR);
	return 0;
}

static int
_lua_widget_set_draw_func(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isluafunction(L, 2))
		return luaL_error(L, "invalid argument");
	if ( !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");

        lua_setfield(L, 1, WIDGET_DRAW);
	return 0;
}

static int
_lua_widget_set_width(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isinteger(L, 2))
		return luaL_error(L, "invalid argument");
	if ( !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");

        lua_setfield(L, 1, WIDGET_WIDTH);
	return 0;

	return 0;
}

static int
_lua_widget_set_height(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isinteger(L, 2))
		return luaL_error(L, "invalid argument");
	if ( !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");

        lua_setfield(L, 1, WIDGET_HEIGHT);
	return 0;

	return 0;
}

static int
_lua_widget_set_timer(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isinteger(L, 2))
		return luaL_error(L, "invalid argument");
	if ( !_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");

	if (_LUA_ISFIELD(L, integer, 1, WIDGET_TIMER) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_FILE) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");

        lua_setfield(L, 1, WIDGET_TIMER);
	return 0;
}

static int
_lua_widget_watch_file(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isstring(L, 2))
		return luaL_error(L, "invalid argument");
	if (!_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");
	if (!is_file_exist(lua_tostring(L, 2)))
		return luaL_error(L, "watch file does not exist");
	if (_LUA_ISFIELD(L, integer, 1, WIDGET_TIMER) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_FILE) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");
	lua_setfield(L, 1, WIDGET_FILE);
	return 0;
}

static int
_lua_widget_watch_device(lua_State *L)
{
	const char *sys;
	if (lua_gettop(L) != 2 || !lua_isstring(L, 2))
		return luaL_error(L, "invalid argument");
	if (!_lua_istable(L, 1, METATABLE_WIDGET))
		return luaL_error(L, "invalid widget");
	sys = lua_tostring(L, 2);
	if (!sys || strstr(sys, "/sys") != sys)
		return luaL_error(L, "invalid subsystem location");
	if (_LUA_ISFIELD(L, integer, 1, WIDGET_TIMER) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_FILE) ||
	    _LUA_ISFIELD(L, string, 1, WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");
	lua_setfield(L, 1, WIDGET_DEV);
	return 0;
}

static int
_lua_new_widget_empty(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, METATABLE_WIDGET);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * @brief users call this function to register a widget,
 *
 * example:
 *
 * twwidget.new_widget({
 *	name = "my widget",
 *	anchor = function
 *	})
 *
 * or then can:
 * w = twwidgets.new_widget()
 * w:anchor('asdfaf')
 * w:set...
 * w:register()
 *
 * or
 * twwidgets.add_builtin_widget('clock')
 */
static int
_lua_register_widget(lua_State *L)
{
	if (lua_gettop(L) == 0)
		return _lua_new_widget_empty(L);
	else if (lua_gettop(L) == 1)
		return _lua_new_widget_from_table(L);
	else
		return luaL_error(L, "invalid number of arguments in new_widget()");
}

static bool
_lua_check_widget_exists(lua_State *L, const struct shell_widget *check)
{
	bool ret = true;
	struct shell_widget *widget;
	int n_widgets;
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);

	n_widgets = lua_rawlen(L, -1);
	for (int i = 0; i < n_widgets; i++) {
		lua_rawgeti(L, -1, i+1);
		widget = lua_touserdata(L, -1);
		lua_pop(L, 1);
		if (shell_widget_builtin(widget) && check == widget)
			ret = false;
		break;
	}
	return ret;
}

static int
_lua_register_builtin_widget(lua_State *L)
{
	const struct shell_widget *builtin = NULL;
	const char *name = luaL_checkstring(L, 1);

	if (lua_gettop(L) != 1)
		return luaL_error(L, "invaild number of arguments.\n");
	if (!(builtin = shell_widget_get_builtin_by_name(name)))
		return  luaL_error(L, "cannot find builtin widget %s\n", name);

	if (!_lua_check_widget_exists(L, builtin))
		return luaL_error(L, "widget %s already added\n", name);

	//upload widgets
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
	lua_pushlightuserdata(L, (void *)builtin);
	lua_rawseti(L, -2, lua_rawlen(L, -2)+1);
	return 0;
}

static int
luaopen_taiwins_shell(lua_State *L)
{
	//lua widget routine
	luaL_newmetatable(L, METATABLE_WIDGET);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	_LUA_REGISTER("watch_file", _lua_widget_watch_file);
	_LUA_REGISTER("watch_device", _lua_widget_watch_device);
	_LUA_REGISTER("add_timer", _lua_widget_set_timer);
	_LUA_REGISTER("brief", _lua_widget_set_anchor);
	_LUA_REGISTER("register", _lua_widget_done);
	_LUA_REGISTER("draw", _lua_widget_set_draw_func);
	_LUA_REGISTER("width", _lua_widget_set_width);
	_LUA_REGISTER("height", _lua_widget_set_height);

	static const luaL_Reg lib[] =
	{
		//TODO: allow set wallpaper function
		{"set_wallpaper", _lua_set_wallpaper},
		{"panel_pos", _lua_set_panel_position},
		{"set_menu", _lua_set_menu},
		{"new_widget", _lua_register_widget},
		{"add_builtin_widget", _lua_register_builtin_widget},
		{NULL, NULL},
	};

	luaL_newlib(L, lib);
	return 1;
}

static struct shell_widget *
clone_widget(struct shell_widget *wig)
{
	struct shell_widget_runtime *runtime, *copy;

	if (shell_widget_builtin(wig))
		return wig;
	runtime = container_of(wig, struct shell_widget_runtime, widget);
	copy = malloc(sizeof(*runtime));
	memcpy(copy, runtime, sizeof(*runtime));
	return &copy->widget;
}

static void
shell_config_fini_lua(struct shell_config *config)
{
	struct shell_widget *widget, *tmp;
	struct shell_widget_runtime *runtime;
	struct desktop_shell *shell =
		container_of(config, struct desktop_shell, config);
	if (!config->config_data)
		return;
	wl_list_for_each_safe(widget, tmp, &shell->shell_widgets,
	                      link) {
		wl_list_remove(&widget->link);
		if (!shell_widget_builtin(widget)) {
			runtime = container_of(widget,
			                       struct shell_widget_runtime,
			                       widget);
			free(runtime);
		}
	}
	lua_close(config->config_data);
}

static char *
shell_config_request_lua_wallpaper(struct shell_config *config)
{
	const char *wallpaper = NULL;
	if (!config->config_data)
		return NULL;
	lua_getfield(config->config_data, LUA_REGISTRYINDEX,
	             REGISTRY_WALLPAPER);
	wallpaper = lua_tostring(L, -1);
	if (wallpaper)
		wallpaper = strdup(wallpaper);
	lua_pop(L, 1);
	return (char *)wallpaper;
}

void *
shell_config_run_lua(struct shell_config *config, const char *path)
{
	bool safe = true;
	lua_State *L;
	int n_widgets = 0;
	struct shell_widget *wig;
	struct desktop_shell *shell =
		container_of(config, struct desktop_shell, config);
	if (!(L = luaL_newstate()))
		return NULL;
	luaL_openlibs(L);

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_CALLBACKS);
	lua_pushlightuserdata(L, shell);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_SHELL);

	luaL_requiref(L, "taiwins_shell", luaopen_taiwins_shell, true);
        luaL_requiref(L, "nuklear", luaopen_nuklear, true);
        //required for widget callback
        nk_love_new_ui(L, NULL);

	safe = safe && !luaL_loadfile(L, path);
	safe = safe && !lua_pcall(L, 0, 0, 0);
	//adding widgets
	if (!safe) {
		lua_newtable(L);
		lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
		lua_close(L);
		config->config_data = NULL;
		L = NULL;
	} else {
		lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_WIDGETS);
		n_widgets = lua_rawlen(L, -1);
		for (int i = 0; i < n_widgets; i++) {
			lua_rawgeti(L, -1, i+1);
			//clone the widget if it is not builtin widget
			wig = clone_widget(lua_touserdata(L, -1));
			wl_list_insert(&shell->shell_widgets, &wig->link);
			lua_pop(L, 1);
		}
		lua_pop(L, 1); //widgets table
	}
	config->fini_config = shell_config_fini_lua;
	config->request_wallpaper = shell_config_request_lua_wallpaper;
	config->config_data = L;

	return L;
}
