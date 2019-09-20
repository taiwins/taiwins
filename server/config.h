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

struct taiwins_config;

/////////////////////////////////////////////////////////
// config options
/////////////////////////////////////////////////////////

enum taiwins_option_type {
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
struct taiwins_option_listener {
	const enum taiwins_option_type type;
	union wl_argument arg;
	struct wl_list link;
	//Apply function need to verify the data first then apply
	bool (*apply)(struct taiwins_config *, struct taiwins_option_listener *);
};

void taiwins_config_add_option_listener(struct taiwins_config *config,
					const char *key,
					struct taiwins_option_listener *listener);


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

struct taiwins_config_component_listener {
	struct wl_list link;
	//called once in taiwins_run_config, for initialize lua metatable and
	//setup functions
	bool (*init)(struct taiwins_config *, lua_State *L,
		     struct taiwins_config_component_listener *);
	/* @param cleanup here indicates zero happens in the run config,
	   please clean the cache config */
	void (*apply)(struct taiwins_config *c, bool cleanup,
		      struct taiwins_config_component_listener *listener);
};

void taiwins_config_add_component(struct taiwins_config *,
				  struct taiwins_config_component_listener *);

void taiwins_config_request_metatable(lua_State *L);

/////////////////////////////////////////////////////////
// list of builtin bindings
/////////////////////////////////////////////////////////

struct taiwins_apply_bindings_listener {
	struct wl_list link;
	bool (*apply)(struct tw_bindings *bindings,
		      struct taiwins_config *config,
		      struct taiwins_apply_bindings_listener *listener);
};

void taiwins_config_add_apply_bindings(struct taiwins_config *,
				      struct taiwins_apply_bindings_listener *);


enum taiwins_builtin_binding_t {
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
const struct taiwins_binding *taiwins_config_get_builtin_binding(struct taiwins_config *,
								 enum taiwins_builtin_binding_t);

/////////////////////////////////////////////////////////
// APIS
/////////////////////////////////////////////////////////

struct taiwins_config *taiwins_config_create(struct weston_compositor *ec,
					     log_func_t messenger);
void taiwins_config_destroy(struct taiwins_config *);

const char *taiwins_config_retrieve_error(struct taiwins_config *);

/**
 * /brief load and apply the config file
 *
 * to support hot reloading, this function can be called from a keybinding. The
 * check has to be there to be sure nothing is screwed up.
 *
 * /param path if not present, use the internal path
 * /return true if config has no problem
 */
bool taiwins_run_config(struct taiwins_config *config, const char *path);


#ifdef __cplusplus
}
#endif







#endif /* EOF */
