/*
 * LUA-Nuklear - MIT licensed; no warranty implied; use at your own risk.
 * authored from 2015-2016 by Micha Mettke, orignally LOVE-Nuklear
 * adapted to LOVE in 2016 by Kevin Harrison
 * adopted to taiwins in 2019 by Sichem
 */

#ifndef NK_LUA_WIDGET_H
#define NK_LUA_WIDGET_H

#include "widget.h"
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <nk_backends.h>
#include <os/file.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;
struct nk_user_font;

struct nk_love_context {
	struct nk_context *nkctx;
	struct nk_user_font *fonts;
	int font_count;
	float *layout_ratios;
	int layout_ratio_count;
	float T[9];
	float Ti[9];
	int transform_allowed;
};

struct shell_widget_runtime {
	struct shell_widget widget;
	lua_State *L;
	struct nk_love_context *runtime;
	char widgetcb[32];
	char anchorcb[32];
};

static int
lua_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	struct shell_widget_runtime *lua_runtime =
		widget->user_data;
	lua_State *L = lua_runtime->L;
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->anchorcb);
	//setup context
	lua_pcall(L, 1, 1, 0);
	const char *encoded = lua_tostring(L, -1);
	strcpy(label->label, encoded);
	return strlen(encoded);
}

static void
lua_widget_cb(struct nk_context *ctx, float width, float height,
	      struct app_surface *app)
{
	//get widget
	struct shell_widget *widget =
		container_of(app, struct shell_widget, widget);
	struct shell_widget_runtime *lua_runtime =
		widget->user_data;
	lua_State *L = lua_runtime->L;
	//fuction
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->widgetcb);
	//ui
	lua_getfield(L, LUA_REGISTRYINDEX, "runtime");
	lua_runtime->runtime = lua_touserdata(L, -1);
	lua_runtime->runtime->nkctx = ctx;

	if (lua_pcall(L, 1, 0, 0)) {
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
#define _LUA_WIDGET_ANCHOR "LUA_WIDGET_ANCHOR"
#define _LUA_WIDGET_DRAWCB "LUA_WIDGET_DRAW"
static const char *_LUA_N_WIDGETS = "N_WIDGET";


/******************************************************************************/
static void
_lua_init_widget_runtime(struct shell_widget_runtime *runtime, lua_State *L)
{
	memset(runtime, 0, sizeof(struct shell_widget_runtime));
	struct shell_widget *widget = &runtime->widget;
	//initialize
	widget->ancre_cb = lua_widget_anchor;
	widget->draw_cb = lua_widget_cb;
	widget->file_path = NULL;
	widget->subsystem = NULL;
	widget->devname = NULL;
	widget->interval = (struct itimerspec){0};
	widget->user_data = runtime;
	runtime->L = L;

}

/******************************************************************************/
static int
_lua_new_widget_from_table(lua_State *L)
{
	char widget_name[32];
	bool registered = false;

	struct shell_widget_runtime *runtime =
		lua_newuserdata(L, sizeof(struct shell_widget_runtime));
	if (!runtime)
		return luaL_error(L, "uneable to create the widget.");
	_lua_init_widget_runtime(runtime, L);
	struct shell_widget *widget = &runtime->widget;
	lua_rawgetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);
	lua_Integer n_widgets = lua_tointeger(L, -1);

	lua_pushvalue(L, 1);
	//brief
	_LUA_GET_TABLE("anchor", function);
	sprintf(runtime->anchorcb, "%s%02d", _LUA_WIDGET_ANCHOR,
		(int)n_widgets);
	lua_setfield(L, LUA_REGISTRYINDEX, runtime->anchorcb);

	if (!_LUA_TABLE_HAS("draw"))
		widget->draw_cb = NULL;
	else {
		_LUA_GET_TABLE("draw", function);
		sprintf(runtime->anchorcb, "%s%02d", _LUA_WIDGET_DRAWCB,
			(int)n_widgets);
		lua_setfield(L, LUA_REGISTRYINDEX, runtime->anchorcb);
	}
	//watcher
	if (_LUA_TABLE_HAS("file_watch") && !registered) {
		const char *file = _LUA_GET_TABLE("file_watch", string);
		widget->file_path = strdup(file);
		lua_pop(L, 1);
		registered = true;
	} else if (_LUA_TABLE_HAS("timer") && !registered) {
		lua_Integer time = _LUA_GET_TABLE("timer,", integer); //inseconds.
		widget->interval = (struct itimerspec){
			.it_value = {time, 0},
			.it_interval = {time, 0},
		};
		lua_pop(L, 1);
		registered = true;
	} else if (_LUA_TABLE_HAS("device_watch") && !registered) {
		const char *file = _LUA_GET_TABLE("device_watch", string);
		widget->subsystem = strdup(file);
		lua_pop(L, 1);
		registered = true;
	}
	//upload widgets
	lua_pushvalue(L, 2);
	sprintf(widget_name, "%s%02d", "widget", (int)n_widgets);
	lua_setfield(L, LUA_REGISTRYINDEX, widget_name);
	//add one widget
	n_widgets+=1;
	lua_pushinteger(L, n_widgets);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_anchor(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	//we can actually check whether this table has the same metatable
	if (_LUA_TABLE_HAS("anchor"))
		return luaL_error(L, "widget already has anchor");

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushstring(L, "anchor");
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_draw_func(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (_LUA_TABLE_HAS("draw"))
		return luaL_error(L, "widget already has draw");

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushstring(L, "draw");
	lua_pushvalue(L, 2);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_set_timer(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (_LUA_TABLE_HAS("timer") ||
	    _LUA_TABLE_HAS("file_watch") ||
	    _LUA_TABLE_HAS("device_watch"))
		return luaL_error(L, "widget already has triggers");
	lua_Integer nsecs = luaL_checkinteger(L, 2);
	lua_pushstring(L, "timer");
	lua_pushinteger(L, nsecs);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_watch_file(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (_LUA_TABLE_HAS("timer") ||
	    _LUA_TABLE_HAS("file_watch") ||
	    _LUA_TABLE_HAS("device_watch"))
		return luaL_error(L, "widget already has triggers");

	const char *file = luaL_checkstring(L, 2);
	if (!is_file_exist(file))
		return luaL_error(L, "widget cannot watch inexistent file");
	lua_pushstring(L, "file_watch");
	lua_pushstring(L, file);
	lua_rawset(L, 1);

	return 0;
}

/******************************************************************************/
static int
_lua_widget_watch_device(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (_LUA_TABLE_HAS("timer") ||
	    _LUA_TABLE_HAS("file_watch") ||
	    _LUA_TABLE_HAS("device_watch"))
		return luaL_error(L, "widget already has triggers");
	const char *file = luaL_checkstring(L, 2);
	//TODO check whether it is a sysfile?
	if (!is_file_exist(file))
		return luaL_error(L, "invalid subsystem location");
	lua_pushstring(L, "file_watch");
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
 * register_widget({
 * name = "my widget",
 * anchor = function
 })
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

/*****************************************************************************/
static int
luaopen_nkwidget(lua_State *L)
{
	luaL_newmetatable(L, "metatable_widget");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	_LUA_REGISTER("watch_file", _lua_widget_watch_file);
	_LUA_REGISTER("watch_device", _lua_widget_watch_device);
	_LUA_REGISTER("set_timer", _lua_widget_set_timer);
	_LUA_REGISTER("anchor", _lua_widget_set_anchor);
	_LUA_REGISTER("register", _lua_widget_done);
	_LUA_REGISTER("draw", _lua_widget_set_draw_func);
	lua_pushinteger(L, 0);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);

	//creating new metatable for
	static const luaL_Reg lib[] = {
		{"new_widget", _lua_register_widget},
	};
	luaL_newlib(L, lib);
	return 1;
}

/*****************************************************************************/
void
shell_widget_release_with_runtime(struct shell_widget *widget)
{

}

/*****************************************************************************/
void
shell_widget_load_script(struct wl_list *head, const char *path)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	//loatd nkwidget
	luaL_requiref(L, "twwidgets", luaopen_nkwidget, true);
	//TODO and then we load nuklear

	if (luaL_dofile(L, path)) {
		const char *err = lua_tostring(L, -1);
		fprintf(stderr, "error in loading widgets: \n");
		fprintf(stderr, "%s\n", err);
		//maybe load default widgets, in this case
	}
}

#undef _LUA_REGISTER
#undef _LUA_GET_TABLE
#undef _LUA_TABLE_HAS
#undef luaL_checkfunction


#ifdef __cplusplus
}
#endif


#endif /* EOF */
