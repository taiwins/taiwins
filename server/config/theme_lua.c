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
#include <wayland-server-core.h>

#include <ctypes/os/file.h>
#include <ctypes/os/os-compatibility.h>
#include <ctypes/helpers.h>
#include <twclient/theme.h>

#include "lua_helper.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/** this is a difficult choice, we would want to apply defaults when missing
 * values in lua script  */
struct tw_theme_default {
	//global border_color
	tw_rgba_t border_color;

	//outer_spacing, inner_spacing = outer_space / 2;
	float border;
	float rounding;
	tw_vec2_t spacing;
	tw_vec2_t padding;

	tw_rgba_t text_color;
	//edit would have a different color than normal background
};

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

static inline struct tw_theme_default *
tw_theme_defaults_from_lua_state(lua_State *L)
{
	struct tw_theme_default *defaults;

	lua_getfield(L, LUA_REGISTRYINDEX, "tw_theme_default");
	defaults = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return defaults;
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
	              "expecting a floating value");
	*value = lua_tonumber(L, -1);
	tw_lua_assert(L, *value >= 0, "expecting positive value");
}

static inline void
tw_theme_add_handle(lua_State *L, theme_option_handle *handle)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	if (tw_lua_isstring(L, -1)) {
		const char *handle_str;
		char *cpy;

		handle->valid = true;
		handle_str = lua_tostring(L, -1);
		cpy = wl_array_add(&theme->string_pool, strlen(handle_str)+1);
		handle->handle = theme->handle_pool.size / sizeof(off_t);
		*(off_t *)wl_array_add(&theme->handle_pool, sizeof(off_t)) =
			(cpy - (char *)(theme->string_pool.data));
		strcpy(cpy, handle_str);
	} else
		handle->valid = false;
}

static inline void
tw_theme_add_bool(lua_State *L, int *value)
{
	tw_lua_assert(L, lua_isboolean(L, -1), "expecting a boolean");
	*value = lua_toboolean(L, -1);
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
	              "expecting an alignment string");
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

static void
tw_theme_add_header_align(lua_State *L, enum tw_style_header_align *align)
{
	const char *alignment;
	tw_lua_assert(L, tw_lua_isstring(L, -1),
	              "expecting an alignment string");
	alignment = lua_tostring(L, -1);
	if (strcasecmp(alignment, "left") == 0)
		*align = TAIWINS_HEADER_LEFT;
	else if (strcasecmp(alignment, "right") == 0)
		*align = TAIWINS_HEADER_RIGHT;
	else
		luaL_error(L, "invalid header alignment");
}

#define TW_THEME_ADD_STYLE(L, name, type, field)                        \
	({ \
		lua_getfield(L, -1, name); \
		if (!lua_isnil(L, -1)) \
			tw_theme_add_##type(L, field); \
		lua_pop(L, 1); \
	})

#define TW_THEME_ADD_STYLE_FALLBACK(L, name, type, field, fb)           \
	({ \
		lua_getfield(L, -1, name); \
		if (!lua_isnil(L, -1)) \
			tw_theme_add_##type(L, field); \
		else \
			*field = *fb; \
		lua_pop(L, 1); \
	})

#define TW_THEME_ADD_STYLE_PREFER(L, name, type, pass, field, pf)        \
	({ \
		lua_getfield(L, -1, name); \
		if (pass) \
			*field = *pf; \
		else if (!lua_isnil(L, -1)) \
			tw_theme_add_##type(L, field); \
		lua_pop(L, 1); \
	})

#define TW_THEME_READ_SEC(L, pos, name, type, field)                    \
	({ \
		lua_getfield(L, pos, name); \
		if (!lua_isnil(L, -1)) { \
			tw_lua_assert(L, lua_istable(L, -1), \
			              name " style must be a table"); \
			tw_theme_add_##type(L, field); \
		} \
		lua_pop(L, 1); \
	})

#define TW_THEME_READ_SEC_FALLBACK(L, pos, name, type, field, fb)       \
	({ \
		lua_getfield(L, pos, name); \
		if (!lua_isnil(L, -1)) { \
			tw_lua_assert(L, lua_istable(L, -1), \
			              name " style must be a table"); \
			tw_theme_add_##type(L, field); \
		} else { \
			*field = *fb; \
		} \
		lua_pop(L, 1); \
	})

#define TW_THEME_READ_SEC_DEFAULT(L, pos, name, type, field, df)        \
	({ \
		*field = *df; \
		lua_getfield(L, pos, name); \
		if (!lua_isnil(L, -1)) { \
			tw_lua_assert(L, lua_istable(L, -1), \
			              name " style must be a table"); \
			tw_theme_add_##type(L, field); \
		} \
		lua_pop(L, 1); \
	})

static void
tw_theme_add_text(lua_State *L, struct tw_style_text *style)
{
	TW_THEME_ADD_STYLE(L, "color", color, &style->color);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "default_font", handle, &style->default_font);
}

static void
tw_theme_add_button(lua_State *L, struct tw_style_button *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);
	struct tw_theme_default *defaults = tw_theme_defaults_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &style->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &style->active);
	TW_THEME_ADD_STYLE_FALLBACK(L, "border_color", color,
	                            &style->border_color, &theme->window.border_color);

	TW_THEME_ADD_STYLE(L, "text_background", color, &style->text_background);
	TW_THEME_ADD_STYLE_FALLBACK(L, "text_normal", color,
	                            &style->text_normal, &defaults->text_color);
	TW_THEME_ADD_STYLE_FALLBACK(L, "text_hover", color,
	                            &style->text_hover, &style->text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "text_active", color,
	                            &style->text_active, &style->text_normal);
	TW_THEME_ADD_STYLE(L, "text_alignment", alignment, &style->text_alignment);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "image_padding", vec2, &style->image_padding);
}

static void
tw_theme_add_toggle(lua_State *L, struct tw_style_toggle *style)
{
	struct tw_theme_default *defaults = tw_theme_defaults_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &style->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &style->active);
	TW_THEME_ADD_STYLE_FALLBACK(L, "border_color", color,
	                            &style->border_color, &defaults->border_color);

	//the cursor is the little dots light up when you click
	TW_THEME_ADD_STYLE(L, "normal_cursor", item, &style->cursor_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_cursor", item,
	                           &style->cursor_hover, &style->cursor_normal);

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal_text", color,
	                            &style->text_normal, &defaults->text_color);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text", color,
	                            &style->text_hover, &style->text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_text", color,
	                            &style->text_active, &style->text_normal);

	TW_THEME_ADD_STYLE(L, "text_background", color,
	                   &style->text_background);
	TW_THEME_ADD_STYLE(L, "alignment", alignment, &style->text_alignment);

	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "spacing", float, &style->spacing);
	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
}

static void
tw_theme_add_selectable(lua_State *L, struct tw_style_selectable *style)
{
	struct tw_theme_default *defaults =
		tw_theme_defaults_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover", item,
	                            &style->hover, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active", item,
	                            &style->pressed, &style->normal);
	//once selected, the selected item will light up in different ways
	TW_THEME_ADD_STYLE(L, "normal_active", item, &style->normal_active);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_active", item,
	                            &style->hover_active, &style->normal_active);
	TW_THEME_ADD_STYLE_FALLBACK(L, "pressed_active", item,
	                            &style->normal_active, &style->normal_active);

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal_text", color,
	                            &style->text_normal, &defaults->text_color);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text", color,
	                            &style->text_hover, &style->text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_text", color,
	                            &style->text_pressed, &style->text_normal);

	TW_THEME_ADD_STYLE(L, "text_background", color, &style->text_background);
	TW_THEME_ADD_STYLE(L, "alignment", alignment, &style->text_alignment);

	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "image_padding", vec2, &style->image_padding);
}

static void
tw_theme_add_progress(lua_State *L, struct tw_style_progress *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);
	struct tw_style_slider *slider = &theme->slider;

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal", item,
	                            &style->normal, &slider->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover", item,
	                            &style->hover, &slider->hover);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active", item,
	                            &style->active, &slider->active);
	TW_THEME_ADD_STYLE_FALLBACK(L, "border_color", color,
	                            &style->border_color, &slider->border_color);

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal_cursor", item,
	                            &style->cursor_normal, &slider->cursor_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_cursor", item,
	                            &style->cursor_hover, &slider->cursor_hover);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_cursor", item,
	                            &style->cursor_active, &slider->cursor_active);
	TW_THEME_ADD_STYLE(L, "cursor_border_color", color,
	                   &style->cursor_border_color);

	TW_THEME_ADD_STYLE_FALLBACK(L, "border", float,
	                            &style->border, &slider->border);
	TW_THEME_ADD_STYLE_FALLBACK(L, "rounding", float,
	                            &style->rounding, &slider->rounding);
	//progress does not have a cursor
	TW_THEME_ADD_STYLE(L, "cursor_border", float, &style->cursor_border);
	TW_THEME_ADD_STYLE(L, "cursor_rounding", float, &style->cursor_rounding);
	TW_THEME_ADD_STYLE_FALLBACK(L, "padding", vec2,
	                            &style->padding, &slider->padding);
}

static void
tw_theme_add_chart(lua_State *L, struct tw_style_chart *style)
{
	TW_THEME_ADD_STYLE(L, "background", item, &style->background);
	TW_THEME_ADD_STYLE(L, "color", color, &style->color);
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);
	TW_THEME_ADD_STYLE(L, "selected_color", color, &style->selected_color);

	//this maybe half the button
	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
}


static void
tw_theme_default_scroll_btn(lua_State *L, struct tw_style_scrollbar *scroll,
                            struct tw_style_button *scroll_btn)
{
	struct tw_theme_default *defaults = tw_theme_defaults_from_lua_state(L);
	scroll_btn->normal = scroll->cursor_normal;
	scroll_btn->hover = scroll->cursor_hover;
	scroll_btn->active = scroll->cursor_active;
	scroll_btn->border_color = scroll->border_color;

	//scroll_btn->text_background
	scroll_btn->text_normal = defaults->text_color;
	scroll_btn->text_hover = defaults->text_color;
	scroll_btn->text_active = defaults->text_color;

	scroll_btn->padding = scroll->padding;
	scroll_btn->text_alignment = TAIWINS_TEXT_CENTER;
	scroll_btn->border = scroll->border;
	scroll_btn->rounding = scroll->rounding;
}

static void
tw_theme_add_scroll(lua_State *L, struct tw_style_scrollbar *style)
{
	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &style->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &style->active);
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);

	TW_THEME_ADD_STYLE(L, "normal_cursor", item, &style->cursor_normal);
	TW_THEME_ADD_STYLE(L, "hover_cursor", item, &style->cursor_hover);
	TW_THEME_ADD_STYLE(L, "active_cursor", item, &style->cursor_active);
	TW_THEME_ADD_STYLE(L, "cursor_border_color", color,
	                   &style->cursor_border_color);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "cursor_border", float, &style->border_cursor);
	TW_THEME_ADD_STYLE(L, "cursor_rounding", float, &style->rounding_cursor);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);

	TW_THEME_ADD_STYLE(L, "show_button", bool, &style->show_buttons);
	if (style->show_buttons) {
		tw_theme_default_scroll_btn(L, style, &style->inc_button);
		tw_theme_default_scroll_btn(L, style, &style->dec_button);
		TW_THEME_READ_SEC(L, -1, "increase_button", button,
		                  &style->inc_button);
		TW_THEME_READ_SEC(L, -1, "decrease_button", button,
		                  &style->dec_button);
	}
}

static void
tw_theme_add_slider(lua_State *L, struct tw_style_slider *style)
{
	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &style->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &style->active);
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);

	TW_THEME_ADD_STYLE(L, "normal_bar", color, &style->bar_normal);
	TW_THEME_ADD_STYLE(L, "hover_bar", color, &style->bar_hover);
	TW_THEME_ADD_STYLE(L, "active_bar", color, &style->bar_active);
	//TODO: this is crazy, merge with other properties
	TW_THEME_ADD_STYLE(L, "filled_bar", color, &style->bar_filled);

	TW_THEME_ADD_STYLE(L, "normal_cursor", item, &style->cursor_normal);
	TW_THEME_ADD_STYLE(L, "hover_cursor", item, &style->cursor_hover);
	TW_THEME_ADD_STYLE(L, "active_cursor", item, &style->cursor_active);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "spacing", vec2, &style->spacing);
	TW_THEME_ADD_STYLE(L, "bar_height", float, &style->bar_height);
	TW_THEME_ADD_STYLE(L, "cursor_size", vec2, &style->cursor_size);

	TW_THEME_ADD_STYLE(L, "show_button", bool, &style->show_buttons);
	if (style->show_buttons) {
		TW_THEME_READ_SEC(L, -1, "increase_button", button,
		                  &style->inc_button);
		TW_THEME_READ_SEC(L, -1, "decrease_button", button,
		                  &style->dec_button);
	}
}

static void
tw_theme_add_edit(lua_State *L, struct tw_style_edit *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);
	struct tw_theme_default *defaults =
		tw_theme_defaults_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover", item,
	                            &style->hover, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active", item,
	                            &style->active, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "border_color", color,
	                            &style->border_color,
	                            &defaults->border_color);
	// fallback to use the system scrollbar
	TW_THEME_READ_SEC_DEFAULT(L, -1, "scrollbar", scroll,
	                          &style->scrollbar, &theme->scrollh);

	TW_THEME_ADD_STYLE(L, "normal_cursor", color, &style->cursor_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_cursor", color,
	                            &style->cursor_hover, &style->cursor_normal);
	TW_THEME_ADD_STYLE(L, "normal_text_cursor", color,
	                   &style->cursor_text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text_cursor", color,
	                            &style->cursor_text_hover,
	                            &style->cursor_text_normal);

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal_text", color,
	                            &style->text_normal, &defaults->text_color);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text", color,
	                            &style->text_hover, &style->text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_text", color,
	                            &style->text_active, &style->text_normal);

	TW_THEME_ADD_STYLE(L, "selected_normal", color, &style->selected_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "selected_hover", color,
	                            &style->selected_hover,
	                            &style->selected_normal);

	TW_THEME_ADD_STYLE(L, "selected_normal_text", color,
	                   &style->selected_text_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "selected_hover_text", color,
	                            &style->selected_text_hover,
	                            &style->selected_text_normal);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "cursor_size", float, &style->cursor_size);
	TW_THEME_ADD_STYLE(L, "scrollbar_size", vec2, &style->scrollbar_size);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "row_padding", float, &style->row_padding);
}

static void
tw_theme_add_property(lua_State *L, struct tw_style_property *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE(L, "hover", item, &style->hover);
	TW_THEME_ADD_STYLE(L, "active", item, &style->active);
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);

	TW_THEME_ADD_STYLE(L, "normal_text", color, &style->label_normal);
	TW_THEME_ADD_STYLE(L, "hover_text", color, &style->label_hover);
	TW_THEME_ADD_STYLE(L, "active_text", color, &style->label_active);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);

	TW_THEME_READ_SEC_FALLBACK(L, -1, "edit", edit, &style->edit,
	                           &theme->edit);
}

static void
tw_theme_add_tab(lua_State *L, struct tw_style_tab *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "background", item, &style->background);
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);

	TW_THEME_READ_SEC_FALLBACK(L, -1, "button", button,
	                           &style->tab_button, &theme->button);
	TW_THEME_READ_SEC_FALLBACK(L, -1, "node_button", button,
	                           &style->node_button, &theme->button);

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "indent", float, &style->indent);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "spacing", vec2, &style->spacing);
}

static void
tw_theme_default_combo_btn(struct tw_style_combo *style,
                           struct tw_style_button *btn)
{
	btn->normal = style->normal;
	btn->hover = style->hover;
	btn->active = style->active;

	btn->text_background = (tw_rgba_t) {0};
	btn->text_normal = style->label_normal;
	btn->text_hover = style->label_hover;
	btn->text_active = style->label_active;

	btn->padding = (tw_vec2_t){2.0, 2.0};
	btn->border = style->border;
	btn->rounding = style->rounding;
}

static void
tw_theme_add_combo(lua_State *L, struct tw_style_combo *style)
{
	struct tw_theme_default *defaults =
		tw_theme_defaults_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover", item,
	                            &style->hover, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active", item,
	                            &style->active, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "border_color", color,
	                            &style->border_color, &defaults->border_color);

	TW_THEME_ADD_STYLE_FALLBACK(L, "normal_text", color,
	                            &style->label_normal, &defaults->text_color);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text", color,
	                            &style->label_hover, &style->label_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_text", color,
	                            &style->label_active, &style->label_normal);

	style->symbol_normal = style->label_normal;
	style->symbol_hover = style->label_hover;
	style->symbol_active = style->label_active;

	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
	TW_THEME_ADD_STYLE(L, "content_padding", vec2, &style->content_padding);
	TW_THEME_ADD_STYLE(L, "button_padding", vec2, &style->button_padding);
	TW_THEME_ADD_STYLE(L, "spacing", vec2, &style->spacing);

	tw_theme_default_combo_btn(style, &style->button);
	TW_THEME_READ_SEC(L, -1, "button", button, &style->button);
}

static void
tw_theme_add_header(lua_State *L, struct tw_style_window_header *style)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);

	TW_THEME_ADD_STYLE(L, "normal", item, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover", item,
	                            &style->hover, &style->normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active", item,
	                            &style->active, &style->normal);

	TW_THEME_READ_SEC_FALLBACK(L, -1, "button", button,
	                           &style->button, &theme->button);

	TW_THEME_ADD_STYLE(L, "normal_text", color, &style->label_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "hover_text", color,
	                            &style->label_hover, &style->label_normal);
	TW_THEME_ADD_STYLE_FALLBACK(L, "active_text", color,
	                            &style->label_active, &style->label_normal);

	//TODO decide whether to default
	TW_THEME_ADD_STYLE(L, "align", header_align, &style->align);
        TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
        TW_THEME_ADD_STYLE(L, "text_padding", vec2, &style->label_padding);
        TW_THEME_ADD_STYLE(L, "spacing", vec2, &style->spacing);
}

static void
tw_theme_add_window(lua_State *L, struct tw_style_window *style)
{
	TW_THEME_ADD_STYLE(L, "background", item, &style->background);
	TW_THEME_ADD_STYLE(L, "scaler", item, &style->scaler);

	//border colors
	TW_THEME_ADD_STYLE(L, "border_color", color, &style->border_color);
	style->popup_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "popup_border_color", color,
	                   &style->popup_border_color);
	style->combo_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "combo_border_color", color,
	                   &style->combo_border_color);
	style->contextual_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "contextual_border_color", color,
	                   &style->contextual_border_color);
	style->menu_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "menu_border_color", color,
	                   &style->menu_border_color);
	style->group_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "group_border_color", color,
	                   &style->group_border_color);
	style->tooltip_border_color = style->border_color;
	TW_THEME_ADD_STYLE(L, "tooltip_border_color", color,
	                   &style->tooltip_border_color);

	//border weight
	TW_THEME_ADD_STYLE(L, "border", float, &style->border);
	style->popup_border = style->border;
	TW_THEME_ADD_STYLE(L, "popup_border", float,
	                   &style->popup_border);
	style->combo_border = style->border;
	TW_THEME_ADD_STYLE(L, "combo_border", float,
	                   &style->combo_border);
	style->contextual_border = style->border;
	TW_THEME_ADD_STYLE(L, "contextual_border", float,
	                   &style->contextual_border);
	style->menu_border = style->border;
	TW_THEME_ADD_STYLE(L, "menu_border", float,
	                   &style->menu_border);
	style->group_border = style->border;
	TW_THEME_ADD_STYLE(L, "group_border", float,
	                   &style->group_border);
	style->tooltip_border = style->border;
	TW_THEME_ADD_STYLE(L, "tooltip_border", float,
	                   &style->tooltip_border);

	TW_THEME_ADD_STYLE(L, "rounding", float, &style->rounding);
        TW_THEME_ADD_STYLE(L, "spacing", vec2, &style->spacing);
        TW_THEME_ADD_STYLE(L, "scrollbar", vec2, &style->scrollbar_size);
        TW_THEME_ADD_STYLE(L, "padding", vec2, &style->padding);
	TW_THEME_ADD_STYLE(L, "popup_padding", vec2, &style->popup_padding);
	TW_THEME_ADD_STYLE(L, "combo_padding", vec2, &style->combo_padding);
	TW_THEME_ADD_STYLE(L, "contextual_padding", vec2,
	                   &style->contextual_padding);
	TW_THEME_ADD_STYLE(L, "menu_padding", vec2, &style->menu_padding);
	TW_THEME_ADD_STYLE(L, "group_padding", vec2, &style->group_padding);
	TW_THEME_ADD_STYLE(L, "tooltip_padding", vec2, &style->tooltip_padding);
}

static void
tw_theme_read_defaults(lua_State *L, struct tw_theme_default *colors)
{
	// window config
	lua_getfield(L, -1, "window");
	if (!lua_isnil(L, -1)) {
		TW_THEME_ADD_STYLE(L, "border_color", color,
		                   &colors->border_color);
		TW_THEME_ADD_STYLE(L, "border", float, &colors->border);
		TW_THEME_ADD_STYLE(L, "rounding", float, &colors->rounding);
		TW_THEME_ADD_STYLE(L, "padding", vec2, &colors->padding);
		TW_THEME_ADD_STYLE(L, "spacing", vec2, &colors->spacing);
	}
	lua_pop(L, 1);
	// text colors
	lua_getfield(L, -1, "text");
	if (!lua_isnil(L, -1))
		TW_THEME_ADD_STYLE(L, "color", color, &colors->text_color);
	lua_pop(L, 1);

	lua_pushlightuserdata(L, colors);
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme_default");
}

/**
 * @Called for processing the theme
 *
 * expecting calling from lua in the form: read_taiwins_theme(style)
 */
int
tw_theme_read(lua_State *L)
{
	struct tw_theme *theme = tw_theme_from_lua_state(L);
	struct tw_theme_default defaults;

	tw_lua_stackcheck(L, 2);
	tw_lua_assert(L, lua_istable(L, 2), "expecting a style table");

	if (theme->handle_pool.data)
		wl_array_release(&theme->handle_pool);
	if (theme->string_pool.data)
		wl_array_release(&theme->string_pool);
	tw_theme_init_default(theme);

	tw_theme_read_defaults(L, &defaults);
	// global
	TW_THEME_READ_SEC(L, 2, "window", window, &theme->window);
	TW_THEME_READ_SEC(L, 2, "text", text, &theme->text);
	// basic widgets
	TW_THEME_READ_SEC(L, 2, "button", button, &theme->button);
	TW_THEME_READ_SEC(L, 2, "contextual_button", button,
	                  &theme->contextual_button);
	TW_THEME_READ_SEC(L, 2, "menu_button", button, &theme->menu_button);
	TW_THEME_READ_SEC(L, 2, "option", toggle, &theme->option);
	TW_THEME_READ_SEC_DEFAULT(L, 2, "checkbox", toggle,
	                          &theme->checkbox, &theme->option);
	TW_THEME_READ_SEC(L, 2, "selectable", selectable, &theme->selectable);
	TW_THEME_READ_SEC(L, 2, "chart", chart, &theme->chart);

	// compond style
	TW_THEME_READ_SEC(L, 2, "slider", slider, &theme->slider);
	//progress relies on slider
	TW_THEME_READ_SEC(L, 2, "progress", progress, &theme->progress);
	TW_THEME_READ_SEC(L, 2, "scroll", scroll, &theme->scrollh);
	theme->scrollv = theme->scrollh;

	TW_THEME_READ_SEC(L, 2, "edit", edit, &theme->edit);
	TW_THEME_READ_SEC(L, 2, "property", property, &theme->property);
	TW_THEME_READ_SEC(L, 2, "tab", tab, &theme->tab);
	TW_THEME_READ_SEC(L, 2, "combo", combo, &theme->combo);
	TW_THEME_READ_SEC(L, 2, "header", header, &theme->window.header);

	return 0;
}


struct tw_theme *
tw_theme_create_global(struct wl_display *display)
{
	static struct tw_theme s_theme;
	return &s_theme;
}
