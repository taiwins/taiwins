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
	struct tw_press press;
	char name[128];
	//we should at least have a interface
	union {
		tw_btn_binding btn_func;
		tw_axis_binding axis_func;
		tw_touch_binding touch_func;
		tw_key_binding key_func;
	};
	void *func;
	void *user_data;
};

/////////////////////////////////////////////////////////
// list of builtin bindings
/////////////////////////////////////////////////////////
enum taiwins_builtin_binding_t {
	//console
	TW_OPEN_CONSOLE_BINDING,
	//shell
	TW_ZOOM_AXIS_BINDING,
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


typedef int (*tw_user_binding_t)(struct taiwins_binding *b);
struct taiwins_config;
typedef bool (*tw_binding_apply_func_t)(void *data, struct tw_bindings *bindings);

struct taiwins_config *taiwins_config_create(struct weston_compositor *ec, log_func_t messenger);

/**
 * /brief run the config file, call this before apply bindings
 */
bool taiwins_run_config(struct taiwins_config *, const char *);

void taiwins_apply_default_config(struct weston_compositor *ec);

void taiwins_config_destroy(struct taiwins_config *);

/* we seperate config with bindings. They don't point to each other */
void taiwins_config_run_apply_binding(struct taiwins_config *, struct tw_bindings *);
/**
 * /brief get the configuration for keybinding
 */
const struct taiwins_binding *taiwins_config_get_builtin_binding(struct taiwins_config *,
								 enum taiwins_builtin_binding_t);

void taiwins_config_add_apply_binding(struct taiwins_config *, void *user_data);


/* this is for registering lua functions */
//it could be a keybinding, touch binding
void taiwins_config_register_user_binding(struct taiwins_config *config,
					  const char *name, void *func);


//then you can add other bindings

#ifdef __cplusplus
}
#endif







#endif /* EOF */
