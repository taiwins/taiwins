#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <compositor.h>
#include <sequential.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include "bindings.h"
#include "config.h"


struct taiwins_config {
	struct weston_compositor *compositor;
	lua_State *L;
	vector_t bindings;
	//this is the place to store all the lua functions
	vector_t lua_bindings;
	//we need this variable to mark configurator failed
	log_func_t print;
	//in terms of xkb_rules, we try to parse it as much as we can
	struct xkb_rule_names rules;
	//we will have quit a few data field
	bool default_floating;
	bool quit;
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


void
taiwins_config_register_binding(struct taiwins_config *config,
				const char *name, void *func)
{
	struct taiwins_binding b = {0};
	strncpy(b.name, name, 128);
	b.func = func;
	vector_append(&config->bindings, &b);
}


static inline struct taiwins_binding *
taiwins_config_find_binding(struct taiwins_config *c, const char *name)
{
	struct taiwins_binding *b = NULL;
	for (int i = 0; i < c->bindings.len; i++) {
		struct taiwins_binding *candidate =
			vector_at(&c->bindings, i);
		if (strcmp(candidate->name, name) == 0) {
			b = candidate;
			break;
		}
	}
	return b;
}

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

static inline int
_lua_bind(lua_State *L, enum tw_binding_type binding_type)
{
	//first argument
	struct taiwins_config *cd = to_user_config(L);
	struct taiwins_binding *binding_to_find = NULL;
	const char *key = NULL;
	//matching string, or lua function
	if (lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		binding_to_find = taiwins_config_find_binding(cd, key);
		if (!binding_to_find || binding_to_find->type != binding_type)
			goto err_binding;
	} else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
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
	if (!binding_seq || !parse_binding(binding_to_find, binding_seq))
		goto err_binding;
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
_lua_noop(lua_State *L)
{
	return 0;
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
	lua_getfield(L, LUA_REGISTRYINDEX, "__config");
	struct taiwins_config *c = lua_touserdata(L, -1);
	//now we need to make another userdata
	lua_pop(L, 1);
	struct _lua_config *lc = lua_newuserdata(L, sizeof(struct _lua_config));
	lc->config = c;
	luaL_getmetatable(L, "compositor");
	lua_setmetatable(L, -2);
	return 1;
}


//////////////////////////////////////////////////////////////////
////////////////////////////// API ///////////////////////////////
//////////////////////////////////////////////////////////////////

struct taiwins_config*
taiwins_config_create(struct weston_compositor *ec, log_func_t log)
{
	lua_State *L = luaL_newstate();
	if (!L)
		return NULL;
	luaL_openlibs(L);
	struct taiwins_config *config =
		calloc(1, sizeof(struct taiwins_config));
	config->compositor = ec;
	config->print = log;
	config->quit = false;
	config->L = L;
	vector_init(&config->bindings, sizeof(struct taiwins_binding), NULL);
	vector_init(&config->lua_bindings, sizeof(struct taiwins_binding), NULL);
	//we can make this into a light-user-data
	lua_pushlightuserdata(L, config);
	//now we have zero elements on stack
	lua_setfield(L, LUA_REGISTRYINDEX, "__config");

	//create metatable and the userdata
	luaL_newmetatable(L, "compositor");
	//you can also use settable
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
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
	vector_destroy(&config->bindings);
	vector_destroy(&config->lua_bindings);
	if (config->rules.layout)
		free((void *)config->rules.layout);
	if (config->rules.model)
		free((void *)config->rules.model);
	if (config->rules.options)
		free((void *)config->rules.options);
	if (config->rules.variant)
		free((void *)config->rules.variant);
	lua_close(config->L);
	free(config);
}

bool
taiwins_run_config(struct taiwins_config *config, const char *path)
{
	int error = luaL_loadfile(config->L, path);
	if (error)
		_lua_error(config, "%s is not a valid config file", path);
	else
		lua_pcall(config->L, 0, 0, 0);
	return (!error);
}


void
taiwins_apply_default_config(struct weston_compositor *ec)
{
	ec->kb_repeat_delay = 400;
	ec->kb_repeat_rate = 40;
}
