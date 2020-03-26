/*
 * theme_lua.c - taiwins server theme lua bindings
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

#include <lauxlib.h>
#include <lua.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <wayland-util.h>

#include <os/file.h>
#include <os/os-compatibility.h>
#include <helpers.h>
#include <theme.h>

#include "lua_helper.h"
#include "taiwins.h"


static bool
is_file_image_type(const char *path)
{
	return is_file_type(path, "png") ||
		is_file_type(path, "svg") ||
		is_file_type(path, "jpg") ||
		is_file_type(path, "jpeg") ||
		is_file_type(path, "tga") ||
		is_file_type(path, "bmp");
}


static inline struct tw_theme *
tw_theme_from_lua_state(lua_State *L)
{
	struct tw_theme *theme;

	lua_getfield(L, LUA_REGISTRYINDEX, "tw_theme");
	theme = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return theme;
}

static int
tw_theme_add_color(lua_State *L, tw_rgba_t *color)
{
	uint32_t rgba_code;
	if (tw_lua_is_rgb(L, -1, &rgba_code))
		color->code = rgba_code;
	else
		luaL_error(L, "%d not a valid color string nor tuple", -1);
	return 0;
}

static inline void
tw_theme_add_float(lua_State *L, float *value)
{
	tw_lua_assert(L, tw_lua_isnumber(L, -1),
	              "expcting a floating value");
	*value = lua_tonumber(L, -1);
}

static void
tw_theme_add_ratio(lua_State *L, float *value)
{
	tw_theme_add_float(L, value);
	if (*value > 1.0 || *value < 0)
		luaL_error(L, "expecting a value between [0, 1]");
}

//I think there is other stuff as well
static void
tw_theme_add_vec2(lua_State *L, tw_vec2_t *vec)
{
	tw_lua_assert(L, lua_istable(L, -1) && lua_rawlen(L, -1) == 2,
	              "expecting a vec2 table");
	lua_rawgeti(L, -1, 1);
	tw_lua_assert(L, tw_lua_isnumber(L, -1), "expecting vec2 with floats");
	vec->x = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, -1, 2);
	tw_lua_assert(L, tw_lua_isnumber(L, -1), "expecting vec2 with floats");
	vec->y = lua_tonumber(L, -1);
	lua_pop(L, 1);
}

//is color or image
static void
tw_theme_add_item(lua_State *L, theme_option_style *option)
{
	struct tw_style_item *item = &option->style;
	uint32_t rgba_code;
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	option->valid = true;
	if (tw_lua_is_rgb(L, -1, &rgba_code)) {
		item->type = TAIWINS_STYLE_COLOR;
		item->data.color.code = rgba_code;
	} else if (tw_lua_isstring(L, -1) && //image
	           is_file_image_type(lua_tostring(L, -1))) {

		const char *path = lua_tostring(L, -1);
		char *cpy = wl_array_add(&theme->string_pool, strlen(path)+1);
		size_t n = theme->handle_pool.size / sizeof(off_t);

		*(off_t *)wl_array_add(&theme->handle_pool, sizeof(off_t)) =
			(cpy - (char *)theme->string_pool.data);
		strcpy(cpy, path);

		item->type = TAIWINS_STYLE_IMAGE;
		item->data.image.handle = n;
	} else {
		option->valid = false;
		luaL_error(L, "error add taiwins style item");
	}
}

static void
tw_theme_add_alignment(lua_State *L, tw_flags *flags)
{
	const char *alignment;

	tw_lua_assert(L, tw_lua_isstring(L, -1),
	              "expecting a alignment string");
	alignment = lua_tostring(L, -1);
	if (strcasecmp(alignment, "left") == 0)
		*flags = TAIWINS_TEXT_LEFT;
	else if (strcasecmp(alignment, "right"))
		*flags = TAIWINS_TEXT_RIGHT;
	else if (strcasecmp(alignment, "center"))
		*flags = TAIWINS_TEXT_CENTER;
	else
		luaL_error(L, "invalid text alignment");
}

#define TW_THEME_ADD_STYLE(L, name, type, field)                        \
	lua_getfield(L, -1, name); \
	if (!lua_isnil(L, -1)) \
		tw_theme_add_##type(L, field); \
	lua_pop(L, 1);

static void
tw_theme_add_button(lua_State *L, struct tw_style_button *button)
{
	tw_lua_assert(L, lua_istable(L, -1), "button style must be a table");
	TW_THEME_ADD_STYLE(L, "normal", item, &button->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &button->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &button->active);
	TW_THEME_ADD_STYLE(L, "border_color", color, &button->border_color);

	TW_THEME_ADD_STYLE(L, "text_background", color, &button->text_background);
	TW_THEME_ADD_STYLE(L, "text_normal", color, &button->text_normal);
	TW_THEME_ADD_STYLE(L, "text_hover", color, &button->text_hover);
	TW_THEME_ADD_STYLE(L, "text_active", color, &button->text_active);
	TW_THEME_ADD_STYLE(L, "text_alignment", alignment, &button->text_alignment);

	TW_THEME_ADD_STYLE(L, "border", ratio, &button->border);
	TW_THEME_ADD_STYLE(L, "rounding", ratio, &button->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &button->padding);
	TW_THEME_ADD_STYLE(L, "image_padding", vec2, &button->image_padding);
	TW_THEME_ADD_STYLE(L, "touch_padding", vec2, &button->touch_padding);
}

//static void tw_theme_add_toggle(lua_State *L, struct tw_style_toggle *toggle) {}

/**
 * @called for processing the theme
 *
 * expecting calling from lua in the form: read_taiwins_theme(style)
 */
int
tw_theme_read(lua_State *L)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	tw_lua_stackcheck(L, 2);
	tw_lua_assert(L, lua_istable(L, 2), "expecting a style table");

	if (theme->handle_pool.data)
		wl_array_release(&theme->handle_pool);
	if (theme->string_pool.data)
		wl_array_release(&theme->string_pool);
	memset(theme, 0, sizeof(struct tw_theme));

	lua_getfield(L, 2, "button");
	tw_theme_add_button(L, &theme->button);
	lua_pop(L, 1);

	return 0;
}

void
tw_theme_init_for_lua(struct tw_theme *theme, lua_State *L)
{
	lua_pushlightuserdata(L, theme);
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme");
	REGISTER_METHOD(L, "read_theme", tw_theme_read);
}
