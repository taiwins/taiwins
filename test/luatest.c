#include <stdio.h>
#include <lua5.2/lua.h>
#include <lua5.2/lualib.h>
#include <lua5.2/lauxlib.h>


int main(int argc, char *argv[])
{
	int status, resultat, i;
	double sum;
	lua_State *L;

	L = luaL_newstate();
	luaL_openlibs(L);
	status = luaL_loadfile(L, "/tmp/script.lua");
	if (status) {
		/* there is something went wrong */
		fprintf(stderr, "there is something went wrong\n");
	}

	lua_newtable(L);
	return 0;
}
