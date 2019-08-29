#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <wayland-server.h>
#include <compositor.h>
#include "../server/config.h"

static bool
dummpy_apply(struct taiwins_config *c, struct taiwins_option_listener *l)
{
	fprintf(stderr, "applying configuration\n");
	return true;
}


struct taiwins_option_listener option = {
	.type = TW_OPTION_RGB,
	.apply = dummpy_apply,
};

/*****************************************************************
 * LUA_BINDINGS 
 ****************************************************************/

#define REGISTER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})

static int
dummy_lua_method_0(lua_State *L)
{
	fprintf(stderr, "dummy method 0 called\n");
	return 0;
}

static int
get_dummy_table(lua_State *L)
{
	lua_newtable(L); //1
	luaL_getmetatable(L, "metatable_dummy"); //3
	lua_setmetatable(L, -2); //2
	
	for (int i = 0; i < 10; i++) {
		lua_newtable(L); //2

		luaL_getmetatable(L, "metatable_dummy"); //3
		lua_setmetatable(L, -2); //2

		lua_pushstring(L, "dummy_field"); //3
		lua_pushstring(L, "haha"); //4
		lua_settable(L, -3); //2

		lua_rawseti(L, -2, i+1);
	}
	return 1;
}

static int
lua_get_dummy_interface(lua_State *L)
{
	lua_newtable(L);
	luaL_getmetatable(L, "metatable_dummy");
	lua_setmetatable(L, -2);
	return 1;
}


static bool
lua_component_init(struct taiwins_config *config, lua_State *L,
		   struct taiwins_config_component_listener *listener)
{
	luaL_newmetatable(L, "metatable_dummy");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	REGISTER_METHOD(L, "dummy_method", dummy_lua_method_0);
	REGISTER_METHOD(L, "get_dummy_table", get_dummy_table);
	lua_pop(L, 1);

	REGISTER_METHOD(L, "get_dummy_interface", lua_get_dummy_interface);
	
	return true;
}

struct taiwins_config_component_listener lua_component = {
	.link = {
		&lua_component.link, &lua_component.link,},
	.init = lua_component_init,
};

int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_compositor *ec = weston_compositor_create(display, NULL);
	struct taiwins_config *config = taiwins_config_create(ec, vprintf);

	taiwins_config_add_option_listener(config, "bigmac", &option);
	taiwins_config_add_component(config, &lua_component);

	taiwins_run_config(config, argv[1]);

	taiwins_config_destroy(config);
	weston_compositor_shutdown(ec);
	weston_compositor_destroy(ec);
	wl_display_terminate(display);
	wl_display_destroy(display);
	return 0;
}
