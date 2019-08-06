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
	void *func;
};


typedef int (*tw_binding_apply_t)(struct taiwins_binding *b, void *userdata);
struct taiwins_config;


struct taiwins_config *taiwins_config_create(struct weston_compositor *ec, log_func_t messenger);

bool taiwins_run_config(struct taiwins_config *, const char *);

void taiwins_config_destroy(struct taiwins_config *);

void taiwins_apply_default_config(struct weston_compositor *ec);

void taiwins_config_register_binding(struct taiwins_config *config,
				     const char *name, void *func);

bool taiwins_config_apply_bindings(struct taiwins_config *config,
				   tw_binding_apply_t func, void *data);


/////////////////////////////////////////////////////////
// list of builtin bindings
/////////////////////////////////////////////////////////
enum taiwins_builtin_binding {
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


//then you can add other bindings

#ifdef __cplusplus
}
#endif







#endif /* EOF */
