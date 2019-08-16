#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <compositor.h>
#include "../server/config.h"


//the basic lua functions

struct luaData {
	int a;
	int b;
	int c;
} test_app;

#define REGISTER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})


static int checkapp(lua_State *L)
{
	void *p = lua_touserdata(L, 1);
	fprintf(stderr, "function called, and return %p, our data is %p\n", *(struct luaData **)p, &test_app);
	if (*(struct luaData **)p != &test_app)
		fprintf(stderr, "test failed\n");
	return 0;
}

static void
error(lua_State *L, const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	lua_close(L);
}

static int
l_sin(lua_State *L)
{
	double d = lua_tonumber(L, 1);
	lua_pushnumber(L, sin(d));
	return 1;
}


static int
set_a(lua_State *L)
{
	if (!lua_istable(L, 1))
		error(L, "it is not a table");
	int digit = lua_tonumber(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, "__userdata");
	struct luaData *data = lua_touserdata(L, -1);
	data->a = digit;
	return 0;
}

static int
set_b(lua_State *L)
{
	if (!lua_istable(L, 1))
		error(L, "it is not a table");
	int digit = lua_tonumber(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, "__userdata");
	struct luaData *data = lua_touserdata(L, -1);
	data->b = digit;
	return 0;
}

static int
set_c(lua_State *L)
{
	if (!lua_istable(L, 1))
		error(L, "it is not a table");
	int digit = lua_tonumber(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, "__userdata");
	struct luaData *data = lua_touserdata(L, -1);
	data->c = digit;
	return 0;
}

int
luaopen_mylib(lua_State *L)
{
	lua_newtable(L); //1
	luaL_getmetatable(L, "_metatable"); //2
	lua_setmetatable(L, -2); //1
	return 1;
}

int main(int argc, char *argv[])
{
	lua_State *L;
	int status;
	L = luaL_newstate();
	luaL_openlibs(L);
	//then we need to set the global variables, push the egl application
	//pointer on the stack.
	test_app.a = 0;
	test_app.b = 0;
	test_app.c = 0;

	lua_pushlightuserdata(L, &test_app); //1
	lua_setfield(L, LUA_REGISTRYINDEX, "__userdata"); //0

	//now we create a new metatable
	luaL_newmetatable(L, "_metatable"); //1
	lua_pushvalue(L, -1); //2
	lua_setfield(L, -2, "__index");
	REGISTER_METHOD(L, "mysin", l_sin);
	REGISTER_METHOD(L, "setA", set_a);
	REGISTER_METHOD(L, "setB", set_b);
	REGISTER_METHOD(L, "setC", set_c);

	//now we need to give user ability to require
	lua_pushcfunction(L, luaopen_mylib);
	lua_setglobal(L, "get_test");

	status = luaL_dofile(L, argv[1]);

	//this is lua function
	fprintf(stdout, "test_app has new value a: %d, b: %d, c: %d\n",
		test_app.a, test_app.b, test_app.c);

	return 0;
}
