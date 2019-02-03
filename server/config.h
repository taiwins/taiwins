#ifndef _SERVER_DEBUG
#define _SERVER_DEBUG

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <compositor.h>
#include <sequential.h>

#ifdef __cplusplus
extern "C" {
#endif

struct taiwins_binding {
	struct tw_press press[5];
	enum tw_binding_type type;
	//the name is probably used for lua
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


#ifdef __cplusplus
}
#endif






#endif /* EOF */
