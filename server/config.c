#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <compositor.h>
#include <sequential.h>
#include "../config.h"

struct taiwins_binding {
	//the top five code
	struct {
		uint32_t code;
		uint32_t modifier;
	} binding[5];

	char name[128];
	void *func;
};

struct taiwins_config {
	//I find this could be a rather smart design, since you dont really want
	//to expose all the bindings at here, you can declare it, then you can
	//have library to populate it, with its name and, well, why not use a
	//hash table for this
	struct weston_compositor *compositor;
	vector_t bindings;
	//this is the place to store all the lua functions
	vector_t lua_bindings;
	//you will need some names about the keybinding
};


static inline void
config_register_binding(struct taiwins_config *config, const char *name, void *func)
{
	struct taiwins_binding b;
	strncpy(b.name, name, 128);
	b.func = func;
	vector_append(&config->bindings, &b);
}


#define REGISER_METHOD(l, name, func)		\
	({lua_pushcfunction(l, func);		\
		lua_setfield(l, -2, name);	\
	})

struct config_data {
	struct weston_compositor *compositor;
	struct taiwins_config config;

};

static struct modifier_table {
	const char *name;
	int32_t mods;
} modifier_tables[] =
{
	//we need to publish this table
	{"C-", MODIFIER_CTRL},
	{"Ctrl-", MODIFIER_CTRL},
	{"M-", MODIFIER_ALT},
	{"Alt-", MODIFIER_ALT},
	{"s-", MODIFIER_SUPER},
	{"Super-", MODIFIER_SUPER},
	//combinations, we don't support random orders for now
	{"C-M-", MODIFIER_CTRL | MODIFIER_ALT},
	{"Ctrl-Meta-", MODIFIER_CTRL | MODIFIER_ALT},
	{"C-s-", MODIFIER_CTRL | MODIFIER_SUPER},
	{"Ctrl-Super-", MODIFIER_CTRL | MODIFIER_SUPER},
	{"M-s-", MODIFIER_ALT | MODIFIER_SUPER},
	{"Alt-Super-", MODIFIER_ALT | MODIFIER_SUPER},

	//with shift
	{"S-C-", MODIFIER_CTRL | MODIFIER_SHIFT},
	{"Super-Ctrl-", MODIFIER_CTRL | MODIFIER_SHIFT},
	{"S-M-", MODIFIER_ALT | MODIFIER_SHIFT},
	{"Super-Alt-", MODIFIER_ALT | MODIFIER_SHIFT},
	{"S-s-", MODIFIER_SUPER | MODIFIER_SHIFT},

};



//afterwards, we will intergrate it in the input
//we need to test these functions
static void
binding_code_parse(const char *code_str, int32_t *mod, int32_t *code)
{
	//try to match the modifiers
	if (!strcmp("C", code_str))
		*mod = MODIFIER_CTRL;
	else if (strcmp("Ctrl", code_str))
		*mod = MODIFIER_ALT;
	else if (strcmp("M", code_str));

	//now we have to parse a key
}

static bool
binding_parse(struct taiwins_binding *b, const char *seq_string)
{
	char seq_copy[128];
	strncpy(seq_copy, seq_string, 128);
	char *save_ptr;
	char *c = strtok_r(seq_copy, " ", &save_ptr);
	int count = 0;
	while (c != NULL && count < 5) {
		int32_t mod, code;
		binding_code_parse(c, &mod, &code);

		c = strtok_r(NULL, " ", &save_ptr);
	}
	if (count >= 5)
		return false;
	return true;
}


static int
lua_bind_key(lua_State *L)
{
	//first argument
	struct config_data *cd = lua_touserdata(L, 1);
	//if the pushed value is a string, we try to find it in our binding
	//table.  otherwise, it should be a lua function. Then we create this
	//binding by its name. Later. you will need functors.

	struct taiwins_binding *binding_to_find = NULL;
	const char *key = NULL;
	//matching string, or lua function
	if (lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		//search in the bindings
	} else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
		//we need to find a way to store this function
		//push the value and store it in the register with a different name
		lua_pushvalue(L, 2);
		binding_to_find = vector_newelem(&cd->config.lua_bindings);
		sprintf(binding_to_find->name, "luabinding:%d", cd->config.lua_bindings.len);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding

	} else {
		//panic??
	}
	//now get the last parameter
	//the last parameter it should has
	const char *binding_seq = lua_tostring(L, 3);
	if (!binding_parse(binding_to_find, binding_seq))
		//panic??
		;

	return 0;
}

//for the configuration, we don't even need to
static int
lua_get_compositor(lua_State *L)
{
	//create the compositor on the lua, I think I can simply use newtable
	struct config_data *cd = lua_newuserdata(L, sizeof(struct config_data));
	//all the metatable
	luaL_getmetatable(L, "compositor");
	/* lua_getfield(L, -1, "__compositor"); */
	/* struct config_data *config_data = lua_touserdata(L, -1); */
	/* lua_pop(L, 1); */
	lua_setmetatable(L, -2);
	//it is so bad, because we don't have any information about what to setup in the table.
	lua_getfield(L, LUA_REGISTRYINDEX, "__compositor");
	struct weston_compositor *compositor = lua_touserdata(L, -1);
	lua_pop(L, 1);
	cd->compositor = compositor;
	//now later on
	return 1;
}


static void prepare_compositor(lua_State *L, struct weston_compositor *compositor)
{
	//we need to register a global method

	luaL_newmetatable(L, "compositor");
	//you can also use settable
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	lua_pushlightuserdata(L, compositor);
	lua_setfield(L, LUA_REGISTRYINDEX, "__compositor");

	//now we register


}
