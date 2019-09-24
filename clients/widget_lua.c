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
#include <lauxlib.h>
#include <nk_backends.h>

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

struct shell_widget_lua_runtime {
	lua_State *L;
	struct nk_love_context *runtime;
	char widgetcb[32];
	char anchorcb[32];
};

static int
lua_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	struct shell_widget_lua_runtime *lua_runtime =
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
	struct shell_widget_lua_runtime *lua_runtime =
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


/*******************************************************************************
 * LUA widget
 ******************************************************************************/
#define TW_REGISTER(name, func)	    \
	lua_pushcfunction(L, func); \
	lua_setfield(L, -2, name)

#ifndef lua_checkfunction
#define lua_checkfunction(L, arg) luaL_checktype(L, arg, LUA_TFUNCTION)

#define _LUA_GET_TABLE(name, type)                                             \
	({ lua_pushstring(L, name);		\
	lua_rawget(L, -2);			\
	luaL_check#type(L, -1); })

#define _LUA_WIDGET_ANCHOR "LUA_WIDGET_ANCHOR"
#define _LUA_WIDGET_DRAWCB "LUA_WIDGET_DRAW"


static const char *_LUA_N_WIDGETS = "N_WIDGET";

static int
_lua_widget_anchor(lua_State *L)
{
}

static int
_lua_widget_watch_file(lua_State *L)
{
	struct shell_widget *shell_widget = luaL_checkudata(L, 1, "metatable_widget");
	(void)shell_widget;
	return 0;
}


static int
_lua_widget_done(lua_State *L)
{
	struct shell_widget *shell_widget = luaL_checkudata(L, 1, "metatable_widget");
	(void)shell_widget;
	//check the integrity of the the widget, then make the widget from it
}

static int
_lua_new_widget_empty(lua_State *L)
{
	struct shell_widget *wig =
		lua_newuserdata(L, sizeof(struct shell_widget));
	(void)wig;
	return 1;
}

static int
_lua_new_widget_from_table(lua_State *L)
{
	char func_name[32];

	struct shell_widget *widget =
		lua_newuserdata(L, sizeof(struct shell_widget));
	if (!widget)
		return luaL_error(L, "enable to create the widget.");

	lua_rawgetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);
	lua_Integer n_widgets = lua_tointeger(L, -1);

	const char * widget_name = _LUA_GET_TABLE("name", string);
	lua_pop(L, 1);

	_LUA_GET_TABLE("anchor", function);
	sprintf(func_name, "%s%02d", _LUA_WIDGET_ANCHOR, n_widgets);
	lua_setfield(L, LUA_REGISTRYINDEX, func_name);
	lua_pop(L, 1);

	_LUA_GET_TABLE("draw", function);
	sprintf(func_name, "%s%02d", _LUA_WIDGET_DRAWCB, n_widgets);
	lua_setfield(L, LUA_REGISTRYINDEX, func_name);
	lua_pop(L, 1);

	n_widgets+=1;
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);

	return 0;
}

/**
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

static int
luaopen_nkwidget(lua_State *L)
{
	static intptr_t n_widgets = 0;
	luaL_newmetatable(L, "metatable_widget");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	TW_REGISTER("watchfile", _lua_widget_watch_file);
	lua_pushinteger(L, 0);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _LUA_N_WIDGETS);

	//creating new metatable for
	static const luaL_Reg lib[] = {
		{"new_widget", _lua_register_widget},
	};
	luaL_newlib(L, lib);
	return 1;
}

void
shell_widget_load_script(struct wl_list *head, const char *path)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	luaL_requiref(L, "nkwidget", luaopen_nkwidget, true);

	if (luaL_dofile(L, path)) {
		const char *err = lua_tostring(L, -1);
		fprintf(stderr, "error in loading widgets: \n");
		fprintf(stderr, "%s\n", err);
		//maybe load default widgets, in this case
	}
}

#undef TW_REGISTER
#undef _LUA_GET_TABLE
#undef luaL_checkfunction


#ifdef __cplusplus
}
#endif


#endif /* EOF */
