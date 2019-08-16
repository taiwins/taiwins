#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <compositor.h>
#include <sequential.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>
#include "bindings.h"
#include "config.h"


struct taiwins_config {
	struct weston_compositor *compositor;
	struct tw_bindings *bindings;
	lua_State *L;
	//we need this variable to mark configurator failed
	log_func_t print;
	//in terms of xkb_rules, we try to parse it as much as we can
	struct xkb_rule_names rules;
	struct wl_list apply_bindings;
	//we will have quit a few data field
	bool default_floating;
	bool quit;
	/* user bindings */
	vector_t lua_bindings;
	struct taiwins_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
};

struct apply_bindings_t {
	struct wl_list node;
	tw_bindings_apply_func_t func;
	struct tw_bindings *bindings;
	void *data;
};


#define REGISTER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})


static inline void
_lua_error(struct taiwins_config *config, const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	config->print(fmt, argp);
	va_end(argp);
}

/*
static bool
parse_binding(struct taiwins_binding *b, const char *seq_string)
{
	char seq_copy[128];
	strncpy(seq_copy, seq_string, 128);
	char *save_ptr;
	char *c = strtok_r(seq_copy, " ,;", &save_ptr);
	int count = 0;
	bool parsed = true;
	while (c != NULL && count < 5 && parsed) {
		parsed = parsed &&
			tw_parse_binding(c, b->type, &b->press[count]);
		c = strtok_r(NULL, " ,;", &save_ptr);
		count += (parsed) ? 1 : 0;
	}
	if (count > 5)
		return false;
	if (count < 5)
		b->press[count].keycode = 0;
	return true && parsed;
}
*/


//////////////////////////////////////////////////////////////////
////////////////////// server functions //////////////////////////
//////////////////////////////////////////////////////////////////

struct _lua_config {
	struct taiwins_config *config;
};

static inline struct taiwins_config *
to_user_config(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__config");
	struct taiwins_config *c = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return c;
}

static struct taiwins_binding *
taiwins_config_find_binding(struct taiwins_config *config,
			    const char *name)
{
	for (int i = 0; i < TW_BUILTIN_BINDING_SIZE; i++) {
		if (strcmp(config->builtin_bindings[i].name, name) == 0)
			return &config->builtin_bindings[i];
	}
	return NULL;
}

static inline int
_lua_bind(lua_State *L, enum tw_binding_type binding_type)
{
	//first argument
	struct taiwins_config *cd = to_user_config(L);
	struct taiwins_binding *binding_to_find = NULL;
	const char *key = NULL;

	if (lua_isstring(L, 2)) { //if it is a built-in binding
		key = lua_tostring(L, 2);
		binding_to_find = taiwins_config_find_binding(cd, key);
		if (!binding_to_find || binding_to_find->type != binding_type)
			goto err_binding;
	} else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) { //user binding
		//we need to find a way to store this function
		//push the value and store it in the register with a different name
		lua_pushvalue(L, 2);
		binding_to_find = vector_newelem(&cd->lua_bindings);
		binding_to_find->type = binding_type;
		sprintf(binding_to_find->name, "luabinding:%d", cd->lua_bindings.len);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding
	} else
		goto err_binding;
	const char *binding_seq = lua_tostring(L, 3);

	/* if (!binding_seq || !parse_binding(binding_to_find, binding_seq)) */
	/*	goto err_binding; */
	return 0;
err_binding:
	cd->quit = true;
	return 0;
}

static int
_lua_bind_key(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_key);
}

static int
_lua_bind_btn(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_btn);
}

static int
_lua_bind_axis(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_axis);
}

static int
_lua_bind_tch(lua_State *L)
{
	return _lua_bind(L, TW_BINDING_tch);
}

static int
_lua_set_keyboard_model(lua_State *L)
{
	struct taiwins_config *c = to_user_config(L);
	if (!lua_isstring(L, 2)) {
		c->quit = true;
		return 0;
	}
	const char *model = lua_tostring(L, 2);
	c->rules.model = strdup(model);
	return 0;
}

static int
_lua_set_keyboard_layout(lua_State *L)
{
	struct taiwins_config *c = to_user_config(L);
	if (!lua_isstring(L, 2)) {
		c->quit = true;
		return 0;
	}
	const char *layout = lua_tostring(L, 2);
	c->rules.layout = strdup(layout);
	return 0;
}

static int
_lua_set_keyboard_options(lua_State *L)
{
	struct taiwins_config *c = to_user_config(L);
	if (!lua_isstring(L, 2)) {
		c->quit = true;
		return 0;
	}
	const char *options = lua_tostring(L, 2);
	c->rules.options = strdup(options);
	return 0;
}



/* usage: compositor.set_repeat_info(100, 40) */
static int
_lua_set_repeat_info(lua_State *L)
{
	struct taiwins_config *c = to_user_config(L);
	if (lua_gettop(L) != 3 ||
	    !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3)) {
		c->quit = true;
		return 0;
	}
	int32_t rate = lua_tointeger(L, 2);
	int32_t delay = lua_tointeger(L, 3);
	c->compositor->kb_repeat_rate = rate;
	c->compositor->kb_repeat_delay = delay;

	return 0;
}


static int
_lua_get_config(lua_State *L)
{
	//since light user data has no metatable, we have to create wrapper for it
	/* lua_getfield(L, LUA_REGISTRYINDEX, "__config"); */

	/* struct taiwins_config *c = lua_touserdata(L, -1); */
	//now we need to make another userdata
	/* lua_pop(L, 1); */
	lua_newtable(L);
	luaL_getmetatable(L, "compositor");
	lua_setmetatable(L, -2);
	return 1;
}

//////////////////////////////////////////////////////////////////
////////////////////////////// API ///////////////////////////////
//////////////////////////////////////////////////////////////////

static void
taiwins_config_apply_default(struct taiwins_config *c)
{
	c->default_floating = true;
	//compositor setup
	c->compositor->kb_repeat_delay = 400;
	c->compositor->kb_repeat_rate = 40;

	//apply bindings
	c->builtin_bindings[TW_OPEN_CONSOLE_BINDING] = (struct taiwins_binding){
		.keypress = {{KEY_P, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_OPEN_CONSOLE",
	};
	c->builtin_bindings[TW_ZOOM_AXIS_BINDING] = (struct taiwins_binding){
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SUPER},
		.type = TW_BINDING_axis,
		.name = "TW_ZOOM_AXIS",
	};
	c->builtin_bindings[TW_MOVE_PRESS_BINDING] = (struct taiwins_binding){
		.btnpress = {BTN_LEFT, MODIFIER_SUPER},
		.type = TW_BINDING_btn,
		.name = "TW_MOVE_VIEW_BTN",
	};
	c->builtin_bindings[TW_FOCUS_PRESS_BINDING] = (struct taiwins_binding){
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

struct taiwins_config*
taiwins_config_create(struct weston_compositor *ec, log_func_t log)
{
	lua_State *L = NULL;
	struct taiwins_config *config = NULL;

	L = luaL_newstate();
	if (!L)
		return NULL;
	luaL_openlibs(L);
	config = calloc(1, sizeof(struct taiwins_config));
	config->L = L;
	config->compositor = ec;
	config->print = log;
	config->quit = false;
	wl_list_init(&config->apply_bindings);
	taiwins_config_apply_default(config);
	vector_init(&config->lua_bindings, sizeof(struct taiwins_binding), NULL);

	//we can make this into a light-user-data
	lua_pushlightuserdata(L, config);
	//now we have zero elements on stack
	lua_setfield(L, LUA_REGISTRYINDEX, "__config");

	//create metatable and the userdata
	luaL_newmetatable(L, "compositor"); //stack 1
	//set index to have a litte
	lua_pushvalue(L, -1); //stack 2
	lua_setfield(L, -2, "__index"); //stack 1
	//register all the callbacks
	REGISTER_METHOD(L, "bind_key", _lua_bind_key);
	REGISTER_METHOD(L, "bind_btn", _lua_bind_btn);
	REGISTER_METHOD(L, "bind_axis", _lua_bind_axis);
	REGISTER_METHOD(L, "bind_touch", _lua_bind_tch);
	REGISTER_METHOD(L, "keyboard_model", _lua_set_keyboard_model);
	REGISTER_METHOD(L, "keyboard_layout", _lua_set_keyboard_layout);
	REGISTER_METHOD(L, "keyboard_options", _lua_set_keyboard_options);
	REGISTER_METHOD(L, "repeat_info", _lua_set_repeat_info);

	lua_pushcfunction(L, _lua_get_config);
	lua_setfield(L, LUA_GLOBALSINDEX, "require_compositor");
	lua_pop(L, 1);

	return config;
}

void
taiwins_config_destroy(struct taiwins_config *config)
{
	/* vector_destroy(&config->lua_bindings); */
	if (config->rules.layout)
		free((void *)config->rules.layout);
	if (config->rules.model)
		free((void *)config->rules.model);
	if (config->rules.options)
		free((void *)config->rules.options);
	if (config->rules.variant)
		free((void *)config->rules.variant);

	vector_destroy(&config->lua_bindings);
	lua_close(config->L);
	free(config);
}

void
taiwins_config_set_bindings(struct taiwins_config *config, struct tw_bindings *b)
{
	config->bindings = b;
}

struct tw_bindings*
taiwins_config_get_bindings(struct taiwins_config *config)
{
	return config->bindings;
}

bool
taiwins_run_config(struct taiwins_config *config, const char *path)
{
	int error = luaL_loadfile(config->L, path);
	if (error)
		_lua_error(config, "%s is not a valid config file", path);
	else
		lua_pcall(config->L, 0, 0, 0);
	struct apply_bindings_t *pos, *tmp;

	wl_list_for_each_safe(pos, tmp, &config->apply_bindings, node)
	{
		pos->func(pos->data, pos->bindings, config);
		free(pos);
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
taiwins_config_register_bindings_funcs(struct taiwins_config *c, struct tw_bindings *b,
				       tw_bindings_apply_func_t func, void *data)
{
	struct apply_bindings_t *ab = malloc(sizeof(struct apply_bindings_t));
	ab->bindings = b;
	ab->func = func;
	ab->data = data;
	wl_list_init(&ab->node);
	wl_list_insert(&c->apply_bindings, &ab->node);
}
