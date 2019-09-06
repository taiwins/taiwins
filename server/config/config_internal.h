#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include "../config.h"


#ifdef __cplusplus
extern "C" {
#endif


struct taiwins_config {
	//configuration path, it is copied on first time runing config
	char path[128];
	struct weston_compositor *compositor;
	struct tw_bindings *bindings;
	lua_State *L;
	log_func_t print;

	struct xkb_rule_names rules;

	struct wl_list lua_components;
	struct wl_list apply_bindings;
	/* vector of list */
	vector_t option_hooks;

	bool default_floating;
	bool quit;
	/* user bindings */
	vector_t lua_bindings;
	/* user bindings */
	struct taiwins_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
};


struct taiwins_option {
	char key[32];
	struct wl_list listener_list;
};


void taiwins_config_init_luastate(struct taiwins_config *c);

bool parse_one_press(const char *str, const enum tw_binding_type type,
		     uint32_t *mod, uint32_t *code);





#ifdef __cplusplus
}
#endif


#endif /* EOF */
