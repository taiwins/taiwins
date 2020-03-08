/*
 * config.h - taiwins config header
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

#ifndef _SERVER_DEBUG
#define _SERVER_DEBUG

#include <stdbool.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <sequential.h>
#include "taiwins.h"
#include "bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tw_config;

/////////////////////////////////////////////////////////
// config options
/////////////////////////////////////////////////////////

enum tw_option_type {
	TW_OPTION_INVALID,
	TW_OPTION_INT,
	TW_OPTION_STR,
	TW_OPTION_BOOL,
	TW_OPTION_ARRAY,
	TW_OPTION_RGB,
};

/**
 * @brief option listener
 *
 * The listener is responsible to provide the data container. You can
 * essenstially use wl_array for variable size data. this is very easy for you
 *
 */
struct tw_option_listener {
	const enum tw_option_type type;
	union wl_argument arg;
	struct wl_list link;
	//Apply function need to verify the data first then apply
	bool (*apply)(struct tw_config *, struct tw_option_listener *);
};

void tw_config_add_option_listener(struct tw_config *config,
					const char *key,
					struct tw_option_listener *listener);


/////////////////////////////////////////////////////////
// lua components
/////////////////////////////////////////////////////////
#define REGISTER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})

static inline int _lua_stackcheck(lua_State *L, int size)
{
	if (lua_gettop(L) != size)
		return luaL_error(L, "invalid number of args, expected %d\n", size);
	return 0;
}

struct tw_config_component_listener {
	struct wl_list link;
	//called once in taiwins_run_config, for initialize lua metatable and
	//setup functions
	bool (*init)(struct tw_config *, lua_State *L,
		     struct tw_config_component_listener *);
	/* @param cleanup here indicates zero happens in the run config,
	   please clean the cache config */
	void (*apply)(struct tw_config *c, bool cleanup,
		      struct tw_config_component_listener *listener);
};

void tw_config_add_component(struct tw_config *,
				  struct tw_config_component_listener *);

void tw_config_request_metatable(lua_State *L);

/////////////////////////////////////////////////////////
// list of builtin bindings
/////////////////////////////////////////////////////////

struct tw_apply_bindings_listener {
	struct wl_list link;
	bool (*apply)(struct tw_bindings *bindings,
		      struct tw_config *config,
		      struct tw_apply_bindings_listener *listener);
};

void tw_config_add_apply_bindings(struct tw_config *,
				      struct tw_apply_bindings_listener *);


enum tw_builtin_binding_t {
	TW_QUIT_BINDING,
	TW_RELOAD_CONFIG_BINDING,
	//QUIT taiwins, rerun configuration
	//console
	TW_OPEN_CONSOLE_BINDING,
	//shell
	TW_ZOOM_AXIS_BINDING,
	TW_ALPHA_AXIS_BINDING,
	//views
	TW_MOVE_PRESS_BINDING,
	TW_FOCUS_PRESS_BINDING,
	//workspace
	TW_SWITCH_WS_LEFT_BINDING,
	TW_SWITCH_WS_RIGHT_BINDING,
	TW_SWITCH_WS_RECENT_BINDING,
	TW_TOGGLE_FLOATING_BINDING,
	TW_TOGGLE_VERTICAL_BINDING,
	TW_VSPLIT_WS_BINDING,
	TW_HSPLIT_WS_BINDING,
	TW_MERGE_BINDING,
	//resize
	TW_RESIZE_ON_LEFT_BINDING,
	TW_RESIZE_ON_RIGHT_BINDING,
	//view cycling
	TW_NEXT_VIEW_BINDING,
	//sizeof
	TW_BUILTIN_BINDING_SIZE
};

/**
 * /brief get the configuration for keybinding
 */
const struct tw_binding *tw_config_get_builtin_binding(struct tw_config *,
                                                            enum tw_builtin_binding_t);

/////////////////////////////////////////////////////////
// APIS
/////////////////////////////////////////////////////////

struct tw_config *tw_config_create(struct weston_compositor *ec,
					     log_func_t messenger);
void tw_config_destroy(struct tw_config *);

const char *tw_config_retrieve_error(struct tw_config *);

/**
 * /brief load and apply the config file
 *
 * to support hot reloading, this function can be called from a keybinding. The
 * check has to be there to be sure nothing is screwed up.
 *
 * /param path if not present, use the internal path
 * /return true if config has no problem
 */
bool tw_config_run(struct tw_config *config, const char *path);


#ifdef __cplusplus
}
#endif







#endif /* EOF */
