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

bool parse_one_press(const char *str,
		     const enum tw_binding_type type,
		     uint32_t *mod, uint32_t *code);



static inline void
_lua_error(struct taiwins_config *config, const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	config->print(fmt, argp);
	va_end(argp);
}


//////////////////////////////////////////////////////////////////
///////////////////// binding functions //////////////////////////
//////////////////////////////////////////////////////////////////
static inline struct taiwins_config *
to_user_config(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__config");
	struct taiwins_config *c = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return c;
}


static inline void
_lua_run_binding(void *data)
{
	struct taiwins_binding *b = data;
	lua_State *L = b->user_data;
	lua_getfield(L, LUA_REGISTRYINDEX, b->name);
	if (lua_pcall(L, 0, 0, 0)) {
		struct taiwins_config *config = to_user_config(L);
		_lua_error(config, "error calling lua bindings\n");
	}
	lua_settop(L, 0);
}

static void
_lua_run_keybinding(struct weston_keyboard *keyboard, const struct timespec *time, uint32_t key,
		    uint32_t option, void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_btnbinding(struct weston_pointer *pointer, const struct timespec *time, uint32_t btn,
		    void *data)
{
	_lua_run_binding(data);
}

static void
_lua_run_axisbinding(struct weston_pointer *pointer,
		     const struct timespec *time,
		     struct weston_pointer_axis_event *event,
		     void *data)
{
	_lua_run_binding(data);
}


static struct taiwins_binding *
_new_lua_binding(struct taiwins_config *config, enum tw_binding_type type)
{
	struct taiwins_binding *b = vector_newelem(&config->lua_bindings);
	b->user_data = config->L;
	b->type = type;
	sprintf(b->name, "luabinding_%x", config->lua_bindings.len);
	switch (type) {
	case TW_BINDING_key:
		b->key_func = _lua_run_keybinding;
		break;
	case TW_BINDING_btn:
		b->btn_func = _lua_run_btnbinding;
		break;
	case TW_BINDING_axis:
		b->axis_func = _lua_run_axisbinding;
	default:
		break;
	}
	return b;
}

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
		uint32_t mod, code;
		parsed = parsed &&
			parse_one_press(c, b->type, &mod, &code);

		switch (b->type) {
		case TW_BINDING_key:
			b->keypress[count].keycode = code;
			b->keypress[count].modifier = mod;
			break;
		case TW_BINDING_btn:
			b->btnpress.btn = code;
			b->btnpress.modifier = mod;
			break;
		case TW_BINDING_axis:
			b->axisaction.axis_event = code;
			b->axisaction.modifier = mod;
			break;
		default: //we dont deal with touch right now
			break;
		}

		c = strtok_r(NULL, " ,;", &save_ptr);
		count += (parsed) ? 1 : 0;
		if (count > 1 && b->type != TW_BINDING_key)
			parsed = false;
	}
	if (count >= 5)
		return false;
	//clean the rest of the bits
	for (int i = count; i < 5; i++) {
		b->keypress[count].keycode = 0;
		b->keypress[count].modifier = 0;
	}
	return true && parsed;
}

static inline struct taiwins_binding *
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

	struct taiwins_binding temp = {0};
	const char *binding_seq = lua_tostring(L, 3);
	temp.type = binding_type;
	if (!binding_seq || !parse_binding(&temp, binding_seq))
		goto err_binding;
	//builtin binding
	if (lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		binding_to_find = taiwins_config_find_binding(cd, key);
		if (!binding_to_find || binding_to_find->type != binding_type)
			goto err_binding;
	}
	//user binding
	else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
		//create a function in the registry so we can call it later.
		binding_to_find = _new_lua_binding(cd, binding_type);
		lua_pushvalue(L, 2);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding
	} else
		goto err_binding;

	//now we copy the binding seq to
	memcpy(binding_to_find->keypress, temp.keypress, sizeof(temp.keypress));
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
	//okay, this totally works
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
	//TODO, reload config binding/kill server binding
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

static void
taiwins_config_init_luastate(struct taiwins_config *c)
{
	if (c->L)
		lua_close(c->L);

	lua_State *L = luaL_newstate();
	if (!L)
		return;
	luaL_openlibs(L);
	c->L = L;
	if (c->lua_bindings.elems)
		vector_destroy(&c->lua_bindings);
	vector_init(&c->lua_bindings, sizeof(struct taiwins_binding), NULL);

	//config userdata
	lua_pushlightuserdata(L, c); //s1
	lua_setfield(L, LUA_REGISTRYINDEX, "__config"); //s0

	//create metatable and the userdata
	luaL_newmetatable(L, "compositor"); //s1
	lua_pushvalue(L, -1); //s2
	lua_setfield(L, -2, "__index"); //s1
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
	lua_setglobal(L, "require_compositor");
	lua_pop(L, 1);
}

//right now this function can run once, if we ever need to run multiple times,
//we need to clean up the
struct taiwins_config*
taiwins_config_create(struct weston_compositor *ec, log_func_t log)
{
	struct taiwins_config *config = calloc(1, sizeof(struct taiwins_config));

	config->compositor = ec;
	config->print = log;
	config->quit = false;

	wl_list_init(&config->apply_bindings);
	taiwins_config_apply_default(config);
	taiwins_config_init_luastate(config);

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

/**
 * /brief run/rerun the configurations.
 *
 * right now we can only run once.
 */
bool
taiwins_run_config(struct taiwins_config *config, struct tw_bindings *bindings, const char *path)
{
	int error = luaL_loadfile(config->L, path);
	if (error)
		_lua_error(config, "%s is not a valid config file", path);
	else
		lua_pcall(config->L, 0, 0, 0);
	struct apply_bindings_t *pos, *tmp;

	//TODO: why the hell it is not linked
	/* weston_binding_list_destroy_all(&config->compositor->key_binding_list); */
	/* weston_binding_list_destroy_all(&config->compositor->button_binding_list); */
	/* weston_binding_list_destroy_all(&config->compositor->axis_binding_list); */
	/* weston_binding_list_destroy_all(&config->compositor->touch_binding_list); */

	//install default keybinding
	wl_list_for_each_safe(pos, tmp, &config->apply_bindings, node)
	{
		pos->func(pos->data, pos->bindings, config);
		free(pos);
	}
	//install user bindings
	for (int i = 0; i < config->lua_bindings.len; i++) {
		struct taiwins_binding *binding = vector_at(&config->lua_bindings, i);
		switch(binding->type) {
		case TW_BINDING_key:
			tw_bindings_add_key(bindings, binding->keypress, binding->key_func, 0, binding);
			break;
		case TW_BINDING_btn:
			tw_bindings_add_btn(bindings, &binding->btnpress, binding->btn_func, binding);
			break;
		case TW_BINDING_axis:
			tw_bindings_add_axis(bindings, &binding->axisaction, binding->axis_func, binding);
			break;
		default:
			break;
		}
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
taiwins_config_register_bindings_funcs(struct taiwins_config *c,
				       tw_bindings_apply_func_t func, void *data)
{
	struct apply_bindings_t *ab = malloc(sizeof(struct apply_bindings_t));
	ab->func = func;
	ab->data = data;
	wl_list_init(&ab->node);
	wl_list_insert(&c->apply_bindings, &ab->node);
}
