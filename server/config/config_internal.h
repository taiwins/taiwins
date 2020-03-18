/*
 * config_internal.h - taiwins config shared header
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

#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include "../config.h"


#ifdef __cplusplus
extern "C" {
#endif


struct tw_config {
	char path[128];	/**< it is copied on first time runing config */
	struct weston_compositor *compositor;
	struct tw_bindings *bindings;
	lua_State *L;
	log_func_t print;
	char *err_msg;
	bool quit;

	struct wl_list lua_components;
	struct wl_list apply_bindings;
	vector_t option_hooks;
	struct { /**< compositor option caches, and invalid values */
		struct xkb_rule_names xkb_rules;
		int32_t kb_repeat; /**< invalid: -1 */
		int32_t kb_delay; /**< invalid: -1 */
	};
	/**< user bindings */
	vector_t lua_bindings;
	/**< builtin bindings */
	struct tw_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
};


void tw_config_init_luastate(struct tw_config *c);

bool parse_one_press(const char *str, const enum tw_binding_type type,
		     uint32_t *mod, uint32_t *code);





#ifdef __cplusplus
}
#endif


#endif /* EOF */
