#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <sequential.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>

#include "config_internal.h"



/* //we should have wl_list as well. */
/* struct apply_bindings_t { */
/*	struct wl_list node; */
/*	tw_bindings_apply_func_t func; */
/*	void *data; */
/* }; */



//////////////////////////////////////////////////////////////////
////////////////////////////// API ///////////////////////////////
//////////////////////////////////////////////////////////////////
static inline void
taiwins_config_set_bindings(struct taiwins_config *config, struct tw_bindings *b)
{
	config->bindings = b;
}

static inline struct tw_bindings*
taiwins_config_get_bindings(struct taiwins_config *config)
{
	return config->bindings;
}

static inline void
swap_listener(struct wl_list *dst, struct wl_list *src)
{
	struct wl_list *pos, *tmp;
	for (pos = src->next, tmp = pos->next;
	     pos != src;
	     pos = tmp, tmp = pos->next) {
		wl_list_remove(pos);
		wl_list_insert(dst, pos);
	}
}

static void
taiwins_config_apply_default(struct taiwins_config *c)
{
	c->default_floating = true;
	//compositor setup
	c->compositor->kb_repeat_delay = 400;
	c->compositor->kb_repeat_rate = 40;
	//apply bindings
	c->builtin_bindings[TW_QUIT_BINDING] = (struct taiwins_binding) {
		.keypress = {{KEY_F12, 0}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_QUIT",
	};
	c->builtin_bindings[TW_RELOAD_CONFIG_BINDING] = (struct taiwins_binding) {
		.keypress = {{KEY_R, MODIFIER_CTRL | MODIFIER_ALT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_RELOAD_CONFIG",
	};
	c->builtin_bindings[TW_OPEN_CONSOLE_BINDING] = (struct taiwins_binding) {
		.keypress = {{KEY_P, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_OPEN_CONSOLE",
	};
	c->builtin_bindings[TW_ZOOM_AXIS_BINDING] = (struct taiwins_binding) {
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SUPER},
		.type = TW_BINDING_axis,
		.name = "TW_ZOOM_AXIS",
	};
	c->builtin_bindings[TW_MOVE_PRESS_BINDING] = (struct taiwins_binding) {
		.btnpress = {BTN_LEFT, MODIFIER_SUPER},
		.type = TW_BINDING_btn,
		.name = "TW_MOVE_VIEW_BTN",
	};
	c->builtin_bindings[TW_FOCUS_PRESS_BINDING] = (struct taiwins_binding) {
		.btnpress = {BTN_LEFT, 0},
		.type = TW_BINDING_btn,
		.name = "TW_FOCUS_VIEW_BTN",
	};
	c->builtin_bindings[TW_SWITCH_WS_LEFT_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_LEFT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_LEFT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RIGHT_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RIGHT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RECENT_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_B, MODIFIER_CTRL}, {KEY_B, MODIFIER_CTRL},
			     {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RECENT_WORKSPACE",
	};
	c->builtin_bindings[TW_TOGGLE_FLOATING_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_SPACE, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_FLOATING",
	};
	c->builtin_bindings[TW_TOGGLE_VERTICAL_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_SPACE, MODIFIER_ALT | MODIFIER_SHIFT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_VERTICAL",
	};
	c->builtin_bindings[TW_VSPLIT_WS_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_V, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_VERTICAL",
	};
	c->builtin_bindings[TW_HSPLIT_WS_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_H, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_HORIZENTAL",
	};
	c->builtin_bindings[TW_MERGE_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_M, MODIFIER_CTRL},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_MERGE",
	};
	c->builtin_bindings[TW_RESIZE_ON_LEFT_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_LEFT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_LEFT",
	};
	c->builtin_bindings[TW_RESIZE_ON_RIGHT_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_RIGHT",
	};
	c->builtin_bindings[TW_NEXT_VIEW_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_J, MODIFIER_ALT | MODIFIER_SHIFT},{0},{0},{0},{0}},
		.type = TW_BINDING_key,
		.name = "TW_NEXT_VIEW",
	};
}


//right now this function can run once, if we ever need to run multiple times,
//we need to clean up the
struct taiwins_config*
taiwins_config_create(struct weston_compositor *ec, log_func_t log)
{
	struct taiwins_config *config =
		calloc(1, sizeof(struct taiwins_config));

	config->compositor = ec;
	config->print = log;
	config->quit = false;
	wl_list_init(&config->lua_components);
	wl_list_init(&config->apply_bindings);
	vector_init_zero(&config->option_hooks,
			 sizeof(struct taiwins_option), NULL);

	return config;
}

//release the config to be reused, we don't remove the apply bindings
//here. Since it maybe called again.
static inline void
_taiwins_config_release(struct taiwins_config *config)
{
	if (config->rules.layout)
		free((void *)config->rules.layout);
	if (config->rules.model)
		free((void *)config->rules.model);
	if (config->rules.options)
		free((void *)config->rules.options);
	if (config->rules.variant)
		free((void *)config->rules.variant);

	vector_destroy(&config->lua_bindings);
	if (config->L)
		lua_close(config->L);
	config->L = NULL;
	if (config->bindings)
		tw_bindings_destroy(config->bindings);
	config->bindings = NULL;
	//release everything but not apply_bindings
	wl_list_init(&config->apply_bindings);
}

void
taiwins_config_destroy(struct taiwins_config *config)
{
	_taiwins_config_release(config);
	free(config);
}


/**
 * @brief swap all the config from one to another.
 *
 * at this point we know for sure we can apply the config. This works even if
 * our dst is a fresh new config. The release function will take care of freeing
 * things.
 */
static void
taiwins_swap_config(struct taiwins_config *dst, struct taiwins_config *src)
{
	_taiwins_config_release(dst);
	//clone everthing.
	dst->L = src->L;
	dst->bindings = src->bindings;
	dst->lua_bindings = src->lua_bindings;
	dst->rules = src->rules;
	dst->default_floating = src->default_floating;
	swap_listener(&dst->apply_bindings, &src->apply_bindings);
	swap_listener(&dst->lua_components, &src->lua_components);

	free(src);
}


static void
taiwins_config_try_config(struct taiwins_config *config)
{
	bool safe = true;

	taiwins_config_init_luastate(config);
	taiwins_config_apply_default(config);

	safe = safe && !luaL_loadfile(config->L, config->path);
	safe = safe && !lua_pcall(config->L, 0, 0, 0);
	//try apply bindings
	struct tw_bindings *bindings = taiwins_config_get_bindings(config);
	struct taiwins_apply_bindings_listener *listener;
	if (safe)
		wl_list_for_each(listener, &config->apply_bindings, link)
			safe = safe && listener->apply(bindings, config, listener);
	if (safe) {
		struct taiwins_binding *binding;
		vector_for_each(binding, &config->lua_bindings) {
			switch (binding->type) {
			case TW_BINDING_key:
				tw_bindings_add_key(bindings, binding->keypress,
						    binding->key_func, 0, binding);
				break;
			case TW_BINDING_btn:
				tw_bindings_add_btn(bindings, &binding->btnpress,
						    binding->btn_func, binding);
				break;
			case TW_BINDING_axis:
				tw_bindings_add_axis(bindings, &binding->axisaction,
						     binding->axis_func, binding);
				break;
			default:
				break;
			}
		}
	}
	config->quit = config->quit && !safe;
}


/**
 * /brief run/rerun the configurations.
 *
 * right now we can only run once.
 */
bool
taiwins_run_config(struct taiwins_config *config, const char *path)
{
	bool error = false;
	if (path) {
		strncpy(config->path, path, 127);
	}
	//create temporary resource
	struct tw_bindings *bindings = tw_bindings_create(config->compositor);
	struct taiwins_config *temp_config = taiwins_config_create(config->compositor, config->print);
	//setup the temporary config
	temp_config->option_hooks = config->option_hooks;
	strcpy(temp_config->path, config->path);
	swap_listener(&temp_config->apply_bindings, &config->apply_bindings);
	swap_listener(&temp_config->lua_components, &config->lua_components);
	taiwins_config_set_bindings(temp_config, bindings);
	//now we try the commits
	taiwins_config_try_config(temp_config);
	error = temp_config->quit;
	if (!error) {
		struct taiwins_option *opt = NULL;
		struct taiwins_option_listener *listener;
		//clean up the bindings we have right now
		tw_bindings_apply(bindings);
		taiwins_swap_config(config, temp_config);
		//run all the hooks registered
		vector_for_each(opt, &config->option_hooks)
			wl_list_for_each(listener, &opt->listener_list, link)
				listener->apply(config, listener);
	} else {
		//TODO problem is that we cannot provide detailed error. Maybe
		//weston_log could do something?
		weston_log("%s is not a valid config file", config->path);
		swap_listener(&config->apply_bindings, &temp_config->apply_bindings);
		swap_listener(&config->lua_components, &temp_config->lua_components);

		taiwins_config_destroy(temp_config);
	}

	return (!error);
}

const struct taiwins_binding *
taiwins_config_get_builtin_binding(struct taiwins_config *c,
				   enum taiwins_builtin_binding_t type)
{
	assert(type < TW_BUILTIN_BINDING_SIZE);
	return &c->builtin_bindings[type];
}


void
taiwins_config_add_apply_bindings(struct taiwins_config *c,
				  struct taiwins_apply_bindings_listener *listener)
{
	wl_list_insert(&c->apply_bindings, &listener->link);
}

void
taiwins_config_add_component(struct taiwins_config *c,
			     struct taiwins_config_component_listener *listener)
{
	wl_list_insert(&c->lua_components, &listener->link);
}


void
taiwins_config_add_option_listener(struct taiwins_config *config, const char *key,
				   struct taiwins_option_listener *listener)
{
	struct taiwins_option *opt;
	bool new_option = true;
	vector_for_each(opt, &config->option_hooks) {
		if (strncmp(opt->key, key, 32) == 0) {
			new_option = false;
			break;
		}
	}
	if (new_option) {
		opt = vector_newelem(&config->option_hooks);
		strncpy(opt->key, key, 32);
		wl_list_init(&opt->listener_list);
	}
	wl_list_init(&listener->link);
	wl_list_insert(&opt->listener_list, &listener->link);
}
