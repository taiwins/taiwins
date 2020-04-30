/*
 * config.h - taiwins config header
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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
#include "backend.h"
#include "bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tw_config;


/*******************************************************************************
 * config option
 ******************************************************************************/

struct tw_option {
	char key[32];
	struct wl_list listener_list;
};

enum tw_option_type {
	TW_OPTION_INVALID,
	TW_OPTION_INT,
	TW_OPTION_STR,
	TW_OPTION_BOOL,
	TW_OPTION_ARRAY,
	TW_OPTION_RGB,
};

/**
 * @brief config option listener
 *
 * Any struct implements this interface would have the ability to respond to a
 * lua call `compositor:option('key', option)`. Those options can be anything
 * from integers to rgb values.
 */
struct tw_option_listener {
	const enum tw_option_type type;
	/** the argument is the source of the data */
	union wl_argument arg;
	struct wl_list link;

	/** the type is verified before passing to listener, but the validity
	 * of the option still need verifed */
	bool (*apply)(struct tw_config *, struct tw_option_listener *);
};

void tw_config_add_option_listener(struct tw_config *config,
                                   const char *key,
                                   struct tw_option_listener *listener);


/*******************************************************************************
 * lua components
 ******************************************************************************/

/**
 * @brief config component
 *
 * a config component adds a lua metatable or implements a function
 */
struct tw_config_component_listener {
	struct wl_list link;
	/* @param cleanup here indicates zero happens in the run config,
	   please clean the cache config */
	void (*apply)(struct tw_config *c, bool cleanup,
		      struct tw_config_component_listener *listener);
};

void tw_config_add_component(struct tw_config *,
                             struct tw_config_component_listener *);

/*******************************************************************************
 * bindings configs
 ******************************************************************************/

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

/*******************************************************************************
 * other APIs
 ******************************************************************************/

struct tw_config *tw_config_create(struct weston_compositor *ec,
                                   log_func_t messenger);
void tw_config_destroy(struct tw_config *);

const char *tw_config_retrieve_error(struct tw_config *);

bool tw_run_default_config(struct tw_config *config);

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
