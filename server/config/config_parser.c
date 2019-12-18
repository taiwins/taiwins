/*
 * config_parser.c - taiwins config parser
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

#include <string.h>
#include <stdbool.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sequential.h>
#include <tree.h>
#include <unistd.h>
#include <strops.h>
#include "config_internal.h"

struct event_map {
	const char *name;
	uint32_t event_code;
};

static const struct event_map modifiers_map[] = {
	{"C", MODIFIER_CTRL}, {"Ctrl", MODIFIER_CTRL},
	{"M", MODIFIER_ALT}, {"Alt", MODIFIER_ALT},
	{"s", MODIFIER_SUPER}, {"Super", MODIFIER_SUPER},
	{"S", MODIFIER_SHIFT}, {"Shift", MODIFIER_SHIFT},
};

static const struct event_map special_keys_table[] = {
	//we are here to deal with any key which is not in asci table
	{"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},
	{"F4", KEY_F4}, {"F5", KEY_F5}, {"F6", KEY_F6},
	{"F7", KEY_F7}, {"F8", KEY_F8}, {"F9", KEY_F9},
	{"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
	//function keys
	{"enter", KEY_ENTER}, {"minus", KEY_MINUS},
	{"del", KEY_DELETE}, {"home", KEY_HOME},
	{"end", KEY_END}, {"pgup", KEY_PAGEUP},
	{"pgdn", KEY_PAGEDOWN},
	{"pause", KEY_PAUSE}, {"break", KEY_BREAK},
	{"scrlk", KEY_SCROLLLOCK}, {"insert", KEY_INSERT},
	{"prtsc", KEY_RESERVED}, //TODO
	//arrows
	{"left", KEY_LEFT}, {"right", KEY_RIGHT},
	{"up", KEY_UP}, {"down", KEY_DOWN},
	{"mute", KEY_MUTE}, {"volume_dn", KEY_VOLUMEDOWN},
	{"volume_up", KEY_VOLUMEUP},
	{"bn_up", KEY_BRIGHTNESSUP}, {"bn_dn", KEY_BRIGHTNESSDOWN},
	{"bs", KEY_BACKSPACE}, {"tab", KEY_TAB},
};

static const struct event_map btns_table[] = {
	//btns
	{"btn_l", BTN_LEFT}, {"btn_r", BTN_RIGHT},
	{"btn_m", BTN_MIDDLE},
};

static const struct event_map axis_table[] = {
	{"axis_x", WL_POINTER_AXIS_HORIZONTAL_SCROLL},
	{"axis_y", WL_POINTER_AXIS_VERTICAL_SCROLL},
};


//now we need to have a asci-table, but since you cannot type chars like esc,
//they are dealt in the special_keys_table
#define PUNCT_START 33

//these are the printable keys which you can type
static struct char_map {
	unsigned char ascii;
	int32_t code;
} chars_table[] = {
	//key_resered is not valid, they are for those printable
	//chars which you cannot type thme directly
	//directly start with exclam
	//[33 ~ 47]
	{'!', KEY_RESERVED},
	{'\"', KEY_RESERVED}, {'#', KEY_NUMERIC_POUND},
	{'$', KEY_RESERVED}, {'%', KEY_RESERVED},
	{'&', KEY_RESERVED},
	{'\'', KEY_APOSTROPHE}, //TODO not sure about this one
	{'(', KEY_RESERVED}, {')', KEY_RESERVED},
	{'*', KEY_NUMERIC_STAR}, {'+', KEY_RESERVED},
	{',', KEY_COMMA},
	{'-', KEY_RESERVED}, //special case, you shouldn't see it
	{'.', KEY_DOT}, {'/', KEY_SLASH},
	//numeric keys [48 ~ 37]
	{'0', KEY_0}, {'1', KEY_1}, {'2', KEY_2},
	{'3', KEY_3}, {'4', KEY_4}, {'5', KEY_5},
	{'6', KEY_6}, {'7', KEY_7}, {'8', KEY_8},
	{'9', KEY_9},
	//[58 ~ 64]
	{':', KEY_RESERVED}, {';', KEY_SEMICOLON},
	{'<', KEY_RESERVED}, {'=', KEY_EQUAL},
	{'>', KEY_RESERVED}, {'?', KEY_RESERVED},
	{'@', KEY_RESERVED},
	//upper keys, [65 ~ 90]
	{'A', KEY_A}, {'B', KEY_B}, {'C', KEY_C}, {'D', KEY_D},
	{'E', KEY_E}, {'F', KEY_F}, {'G', KEY_G}, {'H', KEY_H},
	{'I', KEY_I}, {'J', KEY_J}, {'K', KEY_K}, {'L', KEY_L},
	{'M', KEY_M}, {'N', KEY_N}, {'O', KEY_O}, {'P', KEY_P},
	{'Q', KEY_Q}, {'R', KEY_R}, {'S', KEY_S}, {'T', KEY_T},
	{'U', KEY_U}, {'V', KEY_V}, {'W', KEY_W}, {'X', KEY_X},
	{'Y', KEY_Y}, {'Z', KEY_Z},
	//signs [91 ~ 96]
	{'[', KEY_LEFTBRACE}, {'\\', KEY_BACKSLASH},
	{']', KEY_RIGHTBRACE}, {'^', KEY_RESERVED},
	{'_', KEY_RESERVED}, {'`', KEY_RESERVED}, //TODO,incorrect
	//lowercase char [92 ~ 122]
	{'a', KEY_A}, {'b', KEY_B}, {'c', KEY_C}, {'d', KEY_D},
	{'e', KEY_E}, {'f', KEY_F}, {'g', KEY_G}, {'h', KEY_H},
	{'i', KEY_I}, {'j', KEY_J}, {'k', KEY_K}, {'l', KEY_L},
	{'m', KEY_M}, {'n', KEY_N}, {'o', KEY_O}, {'p', KEY_P},
	{'q', KEY_Q}, {'r', KEY_R}, {'s', KEY_S}, {'t', KEY_T},
	{'u', KEY_U}, {'v', KEY_V}, {'w', KEY_W}, {'x', KEY_X},
	{'y', KEY_Y}, {'z', KEY_Z},
	//signs [123 ~ 126]
	{'{', KEY_RESERVED}, {'|', KEY_RESERVED},
	{'}', KEY_RESERVED}, {'~', KEY_RESERVED},
};


static inline bool
parse_table(const char *ptr, const struct event_map *table, size_t table_len,
	    uint32_t *code)
{
	for (int i = 0; i < table_len; i++) {
		if (strcasecmp(ptr, table[i].name) == 0) {
			*code = table[i].event_code;
			return true;
			break;
		}
	}
	return false;
}

static inline unsigned int
parse_modifier(const char *str)
{
	for (int i = 0; i < NUMOF(modifiers_map); i++) {
		if (strcasecmp(modifiers_map[i].name, str) == 0)
			return modifiers_map[i].event_code;
	}
	return 0;
}


static bool
parse_code(const char *code_str, enum tw_binding_type type, uint32_t *code)
{
	const struct event_map *table = NULL;
	size_t table_len = 0;
	bool parsed = false;
	switch (type) {
	case TW_BINDING_key:
		table = special_keys_table;
		table_len = NUMOF(special_keys_table);
		break;
	case TW_BINDING_btn:
		table = btns_table;
		table_len = NUMOF(btns_table);
		break;
	case TW_BINDING_axis:
		table = axis_table;
		table_len = NUMOF(axis_table);
		break;
	case TW_BINDING_tch:
		break;
	default:
		break;
	}
	if (type == TW_BINDING_tch) {
		*code = true;
		parsed = (strcasecmp(code_str, "tch") == 0);
	}
	else if (strlen(code_str) > 1)
		parsed = parse_table(code_str, table, table_len, code);
	//if it is ascii code
	else if (strlen(code_str) == 1 &&
		 type == TW_BINDING_key &&
		 *code_str >= PUNCT_START &&
		 *code_str <= 126) {
		int c = chars_table[*code_str-PUNCT_START].code;
		parsed = (c != KEY_RESERVED);
		*code = c;
	} else
		return false;
	return parsed;
}

bool
parse_one_press(const char *code_str, const enum tw_binding_type type,
		uint32_t *modifier, uint32_t *code)
{
	char str_cpy[128];
	//a valid token can have at most 4 modifiers and 1 key
	char *toks[5] = {0}, *saved_ptr, *c;
	int count = 0;

	*modifier = 0;
	strop_ncpy(str_cpy, code_str, sizeof(str_cpy));
	if (*code_str == '-' || *(code_str+strlen(code_str)-1) == '-')
		return false;

	c = strtok_r(str_cpy, "-", &saved_ptr);
	while (c && count < 5) {
		toks[count++] = c;
		c = strtok_r(NULL, "-", &saved_ptr);
	}
	if (count >= 5)
		return false;
	//parse the modifiers, this loop avoids any dups S-S wont pass
	for (int i = 0; i < count-1; i++) {
		uint32_t mod = parse_modifier(toks[i]);
		if ((*modifier | mod) == *modifier)
			return false;
		*modifier |= mod;
	}
	//parse the code
	return parse_code(toks[count-1], type, code);
}
