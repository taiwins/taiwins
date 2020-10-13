/*
 * lua_helper.h - taiwins lua helpers header
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

#ifndef TW_LUA_HELPER_H
#define TW_LUA_HELPER_H

#include <stdarg.h>
#include <stdbool.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#ifdef __cplusplus
extern "C" {
#endif


#define REGISTER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})

static inline bool
tw_lua_isnumber(lua_State *L, int pos) {return lua_type(L, pos) == LUA_TNUMBER;}

static inline bool
tw_lua_isstring(lua_State *L, int pos) {return lua_type(L, pos) == LUA_TSTRING;}

static inline bool
tw_lua_istable(lua_State *L, int pos, const char *type)
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
tw_lua_isudata(lua_State *L, int pos, const char *type)
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

static inline int
tw_lua_stackcheck(lua_State *L, int size)
{
	if (lua_gettop(L) != size)
		return luaL_error(L, "invalid number of args, expected %d\n", size);
	return 0;
}

static inline void
tw_lua_assert(lua_State *L, int pass, const char *format, ...)
{
	if (!pass) {
		va_list ap;
		const char *err_str;

		va_start(ap, format);
		err_str = lua_pushvfstring(L, format, ap);
		va_end(ap);
		luaL_error(L, err_str);
	}
}

static inline bool
tw_lua_is_rgb_str(lua_State *L, int pos, uint32_t *code)
{
	bool ret = false;
	int r,g,b;
	if (tw_lua_isstring(L, pos) &&
	    sscanf(lua_tostring(L, pos), "#%2x%2x%2x", &r,&g,&b) == 3)
		ret = true;

	if (ret && code)
		*code = (255 << 24) + (r << 16) + (g << 8) + b;
	return ret;
}

static inline bool
tw_lua_is_rgb_tuple(lua_State *L, int pos, uint32_t *code)
{
	int ret = true;
	int r = -1, g = -1, b = -1;
	int *rgb[3] = { &r, &g, &b };

	if (lua_istable(L, pos) && lua_rawlen(L, pos) == 3) {
		for (int i = 0; i < 3; i++) {
			lua_rawgeti(L, pos, i+1);
			ret = ret && tw_lua_isnumber(L, -1);
			*rgb[i] = lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
	} else
		ret = false;
	ret = ret &&
		(r >= 0 && r <= 255) &&
		(b >= 0 && b <= 255) &&
		(g >= 0 && g <= 255);

	if (code)
		*code = (255 << 24) + (r << 16) + (g << 8) + b;
	return ret;
}

static inline bool
tw_lua_is_rgb_dict(lua_State *L, int pos, uint32_t *code)
{
	int ret = true;
	int r = -1, g = -1, b = -1;
	int *rgb[3] = { &r, &g, &b };
	const char *keys[] = { "r", "g", "b"};

	if (lua_istable(L, pos)) {
		for (int i = 0; i < 3; i++) {
			lua_getfield(L, -1, keys[i]);
			ret = ret && tw_lua_isnumber(L, -1);
			*rgb[i] = lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
	}
	ret = ret &&
		(r >= 0 && r <= 255) &&
		(b >= 0 && b <= 255) &&
		(g >= 0 && g <= 255);
	if (code)
		*code = (255 << 24) + (r << 16) + (g << 8) + b;
	return ret;
}
// can we merge them together
static inline bool
tw_lua_is_rgb(lua_State *L, int pos, uint32_t *code)
{
	return tw_lua_is_rgb_str(L, pos, code) ||
		tw_lua_is_rgb_tuple(L, pos, code) ||
		tw_lua_is_rgb_dict(L, pos, code);
}

static inline bool
tw_lua_is_tuple2(lua_State *L, int pos, int *x, int *y)
{
	bool ret = true;

	if (lua_istable(L, pos) && lua_rawlen(L, pos) == 2) {
		lua_rawgeti(L, pos, 1);
		ret = ret && tw_lua_isnumber(L, -1);
		*x = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_rawgeti(L, pos, 1);
		ret = ret && tw_lua_isnumber(L, -1);
		*y = lua_tonumber(L, -1);
		lua_pop(L, 1);
	} else
		return false;
	return  ret;
}

static inline bool
tw_lua_is_int2str(lua_State *L, int pos, int *x, int *y)
{
	if (tw_lua_isstring(L, pos) &&
	    sscanf(lua_tostring(L, pos), "%dx%d", x,y))
		return true;
	else
		return false;
}

#ifdef __cplusplus
}
#endif

#endif /* EOF */
