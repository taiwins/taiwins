#ifndef _SERVER_DEBUG
#define _SERVER_DEBUG

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <compositor.h>
#include <sequential.h>
#include "bindings.h"

#ifdef __cplusplus
extern "C" {
#endif


struct taiwins_binding {
	char name[32];
	enum tw_binding_type type;
	union {
		struct tw_key_press keypress[MAX_KEY_SEQ_LEN];
		struct tw_btn_press btnpress;
		struct tw_axis_motion axisaction;
	};
	union {
		tw_btn_binding btn_func;
		tw_axis_binding axis_func;
		tw_touch_binding touch_func;
		tw_key_binding key_func;
	};
	//this is lua context
	void *user_data;
};

struct taiwins_config;


/////////////////////////////////////////////////////////
// list of builtin bindings
/////////////////////////////////////////////////////////
enum taiwins_builtin_binding_t {
	//QUIT taiwins, rerun configuration
	//console
	TW_OPEN_CONSOLE_BINDING,
	//shell
	TW_ZOOM_AXIS_BINDING,
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


typedef void (*tw_bindings_apply_func_t)(void *data, struct tw_bindings *bindings, struct taiwins_config *config);

struct taiwins_config *taiwins_config_create(struct weston_compositor *ec, log_func_t messenger);
void taiwins_config_destroy(struct taiwins_config *);


/**
 * /brief register an apply_binding function, call this before run_config
 */
void taiwins_config_register_bindings_funcs(struct taiwins_config *c, tw_bindings_apply_func_t func, void *data);

/**
 * /brief load and apply the config file
 *
 * this may be called from a keybinding function, that is provided in the config file
 */
bool taiwins_run_config(struct taiwins_config *, struct tw_bindings *, const char *);

/**
 * /brief get the configuration for keybinding
 */
const struct taiwins_binding *taiwins_config_get_builtin_binding(struct taiwins_config *,
								 enum taiwins_builtin_binding_t);

#ifdef __cplusplus
}
#endif







#endif /* EOF */
