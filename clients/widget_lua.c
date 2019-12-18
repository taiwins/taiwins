/*
 * widget_lua.c - taiwins client shell widget lua binding
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

#ifndef NK_LUA_WIDGET_H
#define NK_LUA_WIDGET_H

#include "widget.h"
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <nk_backends.h>
#include <os/file.h>

#include <nuklear_love.h>

#ifdef __cplusplus
extern "C" {
#endif

struct shell_widget_runtime {
	struct shell_widget widget;
	lua_State *L;
	struct nk_love_context *runtime;

	int index;
	char widgetcb[32];
	char anchorcb[32];
};

static const char *_LUA_N_WIDGETS = "N_WIDGET";


static inline void
_lua_widget_getfield(lua_State *L, int n)
{
	char widget_name[32];
	sprintf(widget_name, "%s%02d", "widget", n);
	lua_getfield(L, LUA_REGISTRYINDEX, widget_name);
}

static inline void
_lua_widget_setfield(lua_State *L, int n)
{
	char widget_name[32];
	sprintf(widget_name, "%s%02d", "widget", n);
	lua_setfield(L, LUA_REGISTRYINDEX, widget_name);
}

static inline int
_lua_n_widgets(lua_State *L)
{
	lua_rawgetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);
	lua_Integer n_widgets = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return n_widgets;
}

static inline void
_lua_set_n_widgets(lua_State *L, int n)
{
	lua_pushinteger(L, n);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);
}

static int
_lua_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	struct shell_widget_runtime *lua_runtime =
		widget->user_data;
	lua_State *L = lua_runtime->L;
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->anchorcb);
	_lua_widget_getfield(L, lua_runtime->index);
	//setup context
	lua_pcall(L, 1, 1, 0);
	const char *encoded = lua_tostring(L, -1);
	strcpy(label->label, encoded);
	return strlen(encoded);
}

static void
_lua_widget_cb(struct nk_context *ctx, float width, float height,
	       struct app_surface *app)
{
	//get widget
	struct shell_widget *widget =
		container_of(app, struct shell_widget, widget);
	struct shell_widget_runtime *lua_runtime =
		widget->user_data;
	lua_State *L = lua_runtime->L;
	lua_runtime->runtime = nk_love_get_ui(L);
	lua_runtime->runtime->nkctx = ctx;
	//fuction and args
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->widgetcb);
	nk_love_getfield_ui(L);
	_lua_widget_getfield(L, lua_runtime->index);

	if (lua_pcall(L, 2, 0, 0)) {
		//if errors occured, we draw this instead
		const char *error = lua_tostring(L, -1);
		nk_clear(ctx);
		nk_layout_row_dynamic(ctx, 20, 1);
		nk_text_colored(ctx, error, strlen(error), NK_TEXT_CENTERED,
				nk_rgb(255, 0, 0));
	}
}

/*******************************************************************************
 * implementation
 ******************************************************************************/

#define _LUA_REGISTER(name, func)	    \
	lua_pushcfunction(L, func); \
	lua_setfield(L, -2, name)

#ifndef luaL_checkfunction
#define luaL_checkfunction(L, arg) luaL_checktype(L, arg, LUA_TFUNCTION)
#endif

#define _LUA_GET_TABLE(name, type)					\
	({ lua_pushstring(L, name);					\
		lua_rawget(L, -2);					\
		luaL_check##type(L, -1); })

#define _LUA_TABLE_HAS(name)				\
	({ bool nil = false;				\
		lua_pushstring(L, name);		\
		lua_rawget(L, -2);			\
		nil = lua_isnil(L, -1);			\
		lua_pop(L, 1);				\
		!nil;})

#define _LUA_WIDGET_METATABLE "metatable_widget"
#define _LUA_WIDGET_ANCHORCB "LUA_WIDGET_ANCHOR"
#define _LUA_WIDGET_DRAWCB "LUA_WIDGET_DRAW"
#define _LUA_WIDGET_ANCHOR "brief"
#define _LUA_WIDGET_DRAW "draw"
#define	_LUA_WIDGET_FILE "file_watch"
#define _LUA_WIDGET_DEV "device_watch"
#define _LUA_WIDGET_TIMER "timer"
#define _LUA_WIDGET_WIDTH "width"
#define _LUA_WIDGET_HEIGHT "height"


/******************************************************************************/
static void
_lua_init_widget_runtime(struct shell_widget_runtime *runtime, lua_State *L, int index)
{
	memset(runtime, 0, sizeof(struct shell_widget_runtime));
	struct shell_widget *widget = &runtime->widget;
	luaL_getmetatable(L, _LUA_WIDGET_METATABLE);
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
}


/******************************************************************************/
static int
_lua_new_widget_from_table(lua_State *L)
{
	bool registered = false;

	struct shell_widget_runtime *runtime =
		lua_newuserdata(L, sizeof(struct shell_widget_runtime));
	if (!runtime)
		return luaL_error(L, "uneable to create the widget.");
	struct shell_widget *widget = &runtime->widget;
	int n_widgets = _lua_n_widgets(L);
	_lua_init_widget_runtime(runtime, L, (int)n_widgets);

	lua_pushvalue(L, 1); //3
	//brief
	if (!_LUA_TABLE_HAS(_LUA_WIDGET_ANCHOR))
		return luaL_error(L, "widget without %s",
				  _LUA_WIDGET_ANCHOR);
	_LUA_GET_TABLE(_LUA_WIDGET_ANCHOR, function);
	sprintf(runtime->anchorcb, "%s%02d", _LUA_WIDGET_ANCHORCB,
		(int)n_widgets);
	lua_setfield(L, LUA_REGISTRYINDEX, runtime->anchorcb);
	if (!_LUA_TABLE_HAS(_LUA_WIDGET_WIDTH) ||
	    !_LUA_TABLE_HAS(_LUA_WIDGET_HEIGHT))
		return luaL_error(L, "missing geometry for widget");
	widget->w = _LUA_GET_TABLE(_LUA_WIDGET_WIDTH, integer);
	lua_pop(L, 1);
	widget->h = _LUA_GET_TABLE(_LUA_WIDGET_HEIGHT, integer);
	lua_pop(L, 1);
	//draw call.
	if (!_LUA_TABLE_HAS(_LUA_WIDGET_DRAW))
		widget->draw_cb = NULL;
	else {
		_LUA_GET_TABLE(_LUA_WIDGET_DRAW, function);
		sprintf(runtime->widgetcb, "%s%02d", _LUA_WIDGET_DRAWCB,
			(int)n_widgets);
		lua_setfield(L, LUA_REGISTRYINDEX, runtime->widgetcb);
	}
	//watchers
	if (_LUA_TABLE_HAS(_LUA_WIDGET_FILE) && !registered) {
		const char *file =
			_LUA_GET_TABLE(_LUA_WIDGET_FILE, string);
		widget->file_path = strdup(file);
		lua_pop(L, 1);
		registered = true;
	}
	if (_LUA_TABLE_HAS(_LUA_WIDGET_TIMER) && !registered) {
		lua_Integer time =
			_LUA_GET_TABLE(_LUA_WIDGET_TIMER, integer);
		widget->interval = (struct itimerspec){
			.it_value = {time, 0},
			.it_interval = {time, 0},
		};
		lua_pop(L, 1);
		registered = true;
	}  else if (_LUA_TABLE_HAS(_LUA_WIDGET_TIMER) && registered)
		return luaL_error(L, "widget already has triggers");

	if (_LUA_TABLE_HAS(_LUA_WIDGET_DEV) && !registered) {
		const char *file =
			_LUA_GET_TABLE(_LUA_WIDGET_DEV, string);
		widget->subsystem = strdup(file);
		lua_pop(L, 1);
		registered = true;
	}  else if (_LUA_TABLE_HAS(_LUA_WIDGET_DEV) && registered)
		return luaL_error(L, "widget already has triggers");

	//upload widgets
	lua_pushvalue(L, 2);
	_lua_widget_setfield(L, n_widgets);
	_lua_set_n_widgets(L, n_widgets+1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_anchor(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	//we can actually check whether this table has the same metatable
	lua_pushvalue(L, 1); //because we have 2 element, we need to push here
	if (_LUA_TABLE_HAS(_LUA_WIDGET_ANCHOR))
		return luaL_error(L, "widget already has %s",
			_LUA_WIDGET_ANCHOR);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushstring(L, _LUA_WIDGET_ANCHOR);
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_width(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_WIDTH))
		return luaL_error(L, "widget already has width");
	luaL_checkinteger(L, 2);
	lua_pushstring(L, _LUA_WIDGET_WIDTH);
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_height(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_HEIGHT))
		return luaL_error(L, "widget already has width");
	luaL_checkinteger(L, 2);
	lua_pushstring(L, _LUA_WIDGET_HEIGHT);
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_draw_func(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_DRAW))
		return luaL_error(L, "widget already has %s",
			_LUA_WIDGET_DRAW);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushstring(L, _LUA_WIDGET_DRAW);
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_timer(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_TIMER) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_FILE) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");
	lua_Integer nsecs = luaL_checkinteger(L, 2);
	lua_pushstring(L, _LUA_WIDGET_TIMER);
	lua_pushinteger(L, nsecs);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_watch_file(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_TIMER) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_FILE) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");

	const char *file = luaL_checkstring(L, 2);
	if (!is_file_exist(file))
		return luaL_error(L, "widget cannot watch inexistent file");
	lua_pushstring(L, _LUA_WIDGET_FILE);
	lua_pushstring(L, file);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_watch_device(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	if (_LUA_TABLE_HAS(_LUA_WIDGET_TIMER) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_FILE) ||
	    _LUA_TABLE_HAS(_LUA_WIDGET_DEV))
		return luaL_error(L, "widget already has triggers");
	const char *file = luaL_checkstring(L, 2);
	//TODO check whether it is a sysfile?
	if (!is_file_exist(file))
		return luaL_error(L, "invalid subsystem location");
	lua_pushstring(L, _LUA_WIDGET_DEV);
	lua_pushstring(L, file);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_done(lua_State *L)
{
	return _lua_new_widget_from_table(L);
}

/******************************************************************************/
static int
_lua_new_widget_empty(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, _LUA_WIDGET_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}


/******************************************************************************
 * users call this function to register a widget,
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

	assert(lua_gettop(L) == 1 || lua_gettop(L) == 0);
	if (lua_gettop(L) == 0)
		return _lua_new_widget_empty(L);
	else if (lua_gettop(L) == 1)
		return _lua_new_widget_from_table(L);
	else
		return luaL_error(L, "invalid number of arguments.");
}

static int
_lua_register_builtin(lua_State *L)
{
	const struct shell_widget *builtin = NULL;
	const char *name = luaL_checkstring(L, 1);

	if (lua_gettop(L) != 1)
		return luaL_error(L, "invaild number of arguments.");
	if (!(builtin = shell_widget_get_builtin_by_name(name)))
		return  luaL_error(L, "cannot find builtin widget %s", name);

	struct shell_widget_runtime *runtime =
		lua_newuserdata(L, sizeof(struct shell_widget_runtime));
	if (!runtime)
		return luaL_error(L, "uneable to create the widget.");
	struct shell_widget *widget = &runtime->widget;
	int n_widgets = _lua_n_widgets(L);
	_lua_init_widget_runtime(runtime, L, n_widgets);
	memcpy(widget, builtin, sizeof(struct shell_widget));
	//create duplicate strings
	if (widget->file_path)
		widget->file_path = strdup(widget->file_path);
	if (widget->subsystem)
		widget->file_path = strdup(widget->subsystem);

	//upload widgets
	lua_pushvalue(L, 2);
	_lua_widget_setfield(L, n_widgets);
	_lua_set_n_widgets(L, n_widgets+1);
	return 0;
}

/*****************************************************************************/
static int
luaopen_nkwidget(lua_State *L)
{
	luaL_newmetatable(L, "metatable_widget");
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
	lua_pushinteger(L, 0);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);

	//creating new metatable for
	static const luaL_Reg lib[] = {
		{"new_widget", _lua_register_widget},
		{"add_builtin", _lua_register_builtin},
	};
	luaL_newlib(L, lib);
	return 1;
}

/*****************************************************************************/
void
shell_widget_release_with_runtime(struct shell_widget *widget)
{
	struct shell_widget_runtime *runtime =
		container_of(widget, struct shell_widget_runtime, widget);
	int n = _lua_n_widgets(L);
	//release with reference counter
	if (n == 0)
		lua_close(runtime->L);
	else
		_lua_set_n_widgets(runtime->L, n-1);
}

/*****************************************************************************/
void
shell_widgets_load_script(struct wl_list *head, struct tw_event_queue *queue,
			  const char *path)
{
	struct shell_widget *widget, *tmp;
	lua_State *L = luaL_newstate();

	luaL_openlibs(L);
	luaL_requiref(L, "twwidgets", luaopen_nkwidget, true);
	luaL_requiref(L, "nuklear", luaopen_nuklear, true);
	nk_love_new_ui(L, NULL);
	//TODO and then we load nuklear

	if (luaL_dofile(L, path)) {
		const char *err = lua_tostring(L, -1);
		fprintf(stderr, "error in loading widgets: \n");
		fprintf(stderr, "%s\n", err);
		//if you can make string int userdata, it would be great
		//clean up existing widgets in the lua state to avoid leaks
		int n_widgets = _lua_n_widgets(L);
		for (int i = 0; i < n_widgets; i++) {
			_lua_widget_getfield(L, i);
			struct shell_widget_runtime *runtime =
				luaL_checkudata(L, -1, _LUA_WIDGET_METATABLE);
			//widget is not in queue, it will do nothing.
			shell_widget_disactivate(&runtime->widget, queue);
		}
		return;
	}

	//remove current widgets if any
	wl_list_for_each_safe(widget, tmp, head, link) {
		wl_list_remove(&widget->link);
		shell_widget_disactivate(widget, queue);
	}
	//insert into headers
	lua_Integer n_widgets = _lua_n_widgets(L);
	for (int i = 0; i < n_widgets; i++) {
		_lua_widget_getfield(L, i);
		struct shell_widget_runtime *runtime =
			luaL_checkudata(L, -1, _LUA_WIDGET_METATABLE);
		wl_list_insert(head, &runtime->widget.link);
		lua_pop(L, 1);
	} lua_pop(L, 1);
}

#undef _LUA_REGISTER
#undef _LUA_GET_TABLE
#undef _LUA_TABLE_HAS
#undef luaL_checkfunction


#ifdef __cplusplus
}
#endif


#endif /* EOF */
