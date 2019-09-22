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

/**
 * users call this function to register a widget,
 *
 * register_widget({
 * name = "my widget",
 * anchor = function
 })
 */
static int
nk_lua_register_widget(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	struct shell_widget *widget = malloc(sizeof(struct shell_widget));
	//I really dont know what to do.
	if (!widget)
		return luaL_error(L, "enable to create the widget.");
	lua_pushstring(L, "name");
	lua_rawget(L, -2);

	const char *widget_name = luaL_checkstring(L, -1);
	lua_pushstring(L, "anchor");
	lua_rawget(L, -3);
	luaL_checktype(L, -1, LUA_TFUNCTION);
	//test anchor function
	lua_setfield(L, LUA_REGISTRYINDEX, "widget_anchor");
	//get widget_function
	lua_pushstring(L, "draw");
	lua_rawget(L, -3);
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_setfield(L, LUA_REGISTRYINDEX, "widget_drawcb");
	return 0;
}

static void
nk_lua_widget_watch_file(lua_State *L)
{
}

void
shell_widget_load_script(struct wl_list *head, const char *path)
{
	//you need basically create a new lua_State
	lua_State *L = luaL_newstate();


	//register some methods here
}



#ifdef __cplusplus
}
#endif


#endif /* EOF */
