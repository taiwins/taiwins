#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

//the basic lua functions

struct luaData {
	int a;
	int b;
	int c;
} test_app;


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

static int l_sin(lua_State *L)
{
	double d = lua_tonumber(L, 1);
	lua_pushnumber(L, sin(d));
	return 1;
}

static const struct luaL_Reg mylib[] = {
	{"mysin", l_sin},
	{NULL, NULL},
};


int
luaopen_mylib(lua_State *L)
{
	lua_newtable(L);

	return 1;
}

int main(int argc, char *argv[])
{
	lua_State *L;
	int status;
	L = luaL_newstate();
	luaL_openlibs(L);
	//we need to define some global
	lua_pushcfunction(L, l_sin);
	lua_setglobal(L, "mysin");
	//then we need to set the global variables, push the egl application
	//pointer on the stack.

	void *ptr = lua_newuserdata(L, sizeof(void *));
	*(struct luaData **)ptr = &test_app;
	lua_pushlightuserdata(L, ptr);
	lua_setglobal(L, "application"); //it pops
	lua_pushcfunction(L, checkapp);
	lua_setglobal(L, "check_variable"); //it pops, so we shouldn't have any problem with that

	status = luaL_dofile(L, argv[1]);

	lua_getglobal(L, "dummy");
	lua_pushnumber(L, 1);
	if (lua_pcall(L, 1, 0, 0) != 0) {
		fprintf(stderr, "calling function problem %s\n",
			lua_tostring(L, -1));
	}
	return 0;
}
