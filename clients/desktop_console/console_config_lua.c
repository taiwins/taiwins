#include <stdbool.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-util.h>

#include "console.h"
#include "helpers.h"
#include "os/file.h"

/* static data used only in this function */
#define MODULES_COUNT "_module_count"
#define MODULES_COLLECTION "_modules"
#define MODULE_METATABLE "_module_metatable"
#define MODULE_IMG_METABLE "_module_img"
#define EMPTY_IMAGE "_module_empty_img"
#define MODULE_TABLE_FORMAT "_lua_table%s"
#define MODULE_UDATA_FORMAT "_lua_udata%s"
#define CONSOLE "_console"

static lua_State *s_L = NULL;
static pthread_mutex_t s_lua_search_lock;
static pthread_mutex_t s_lua_exec_lock;

/*******************************************************************************
 * lua helpers
 ******************************************************************************/

#define _LUA_REGISTER(name, func)	    \
	lua_pushcfunction(L, func); \
	lua_setfield(L, -2, name)

#define _LUA_ISFIELD(L, type, pos, name)                                \
	({ \
		bool ret = false; \
		lua_getfield(L, pos, name); \
		ret = lua_is##type(L, -1); \
		lua_pop(L, 1); \
		ret; \
	})

static inline bool
_lua_istable(lua_State *L, int pos, const char *type)
{
	bool same = false;
	if (!lua_istable(L, pos))
		return false;
	if (lua_getmetatable(L, pos) != 0) { //+1
		luaL_getmetatable(L, type); //+2
		same = lua_compare(L, -1, -2, LUA_OPEQ);
		lua_pop(L, 2);
	}
	return same;
}

static inline void *
_lua_isudata(lua_State *L, int pos, const char *type)
{
	bool same = false;
	if (!lua_isuserdata(L, pos))
		return NULL;
	if (lua_getmetatable(L, pos) != 0) {
		luaL_getmetatable(L, type);
		same = lua_compare(L, -1, -2, LUA_OPEQ);
		lua_pop(L, 2);
	}
	return same ? lua_touserdata(L, pos) : NULL;
}

static inline struct desktop_console *
_lua_to_console(lua_State *L)
{
	struct desktop_console *console;

	lua_getfield(L, LUA_REGISTRYINDEX, CONSOLE);
	console = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return console;
}

static inline int
_lua_n_console_modules(lua_State *L)
{
	int n;

	lua_getfield(L, LUA_REGISTRYINDEX, MODULES_COUNT);
	n = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return n;
}

static inline void
_lua_add_console_module(lua_State *L)
{
	int n = _lua_n_console_modules(L);
	lua_pushinteger(L, n+1);
	lua_setfield(L, LUA_REGISTRYINDEX, MODULES_COUNT);
}

/*******************************************************************************
 * lua module API
 ******************************************************************************/
static int
console_lua_module_get_table(lua_State *L, struct console_module *module)
{
	char table_name[64];
	sprintf(table_name, MODULE_TABLE_FORMAT, module->name);
	lua_getfield(L, LUA_REGISTRYINDEX, table_name);
	return 1;
}

static int
console_lua_module_search(struct console_module *module,
                          const char *keyword, vector_t *result)
{
	int nresult, err;
	const char *err_msg;
	lua_State *L = module->user_data;
	console_search_entry_t *entry, tmp;

	vector_init_zero(result, sizeof(console_search_entry_t),
			 search_entry_free);

	pthread_mutex_lock(&s_lua_search_lock);
	//calling
	console_lua_module_get_table(L, module); //+1
	lua_getfield(L, -1, "search"); //+2

	lua_pushvalue(L, -2); //+3: first argument, the table itself
	lua_pushstring(L, keyword); //+4: second argument, the search string
	err = lua_pcall(L, 2, 1, 0); //+1:  2 argument, 1 result, no stacktrace

	if (err != LUA_OK) {
		err_msg = lua_tostring(L, -1);
		fprintf(stderr, "%s:search err:%s\n",
		        module->name,err_msg);
		lua_pop(L, 1);
	} else {
		//collect results as: { .entry = "foo", image = udata}
		nresult = lua_rawlen(L, -1);
		for (int i = 0; i < nresult; i++) {
			struct nk_image *img;
			const char *string;

			lua_rawgeti(L, -1, i+1);
			if (!lua_istable(L, -1)) {
				lua_pop(L, 1);
				continue;
			}
			//clean up first
			tmp.pstr = NULL;
			tmp.sstr[0] = '\0';

			lua_getfield(L, -1, "entry");
			lua_getfield(L, -2, "image");
			//convert result
			string = lua_tostring(L, -2);
			img = _lua_isudata(L, -1, MODULE_IMG_METABLE);
			if (string && strlen(string) < 32)
				strcpy(tmp.sstr, string);
			else if (string)
				tmp.pstr = strdup(string);
			if (img)
				tmp.img = *img;
			else
				tmp.img = (struct nk_image){0};
			//pop the results + the current elem
			lua_pop(L, 3);

			if (!search_entry_empty(&tmp)) {
				entry = vector_newelem(result);
				*entry = tmp;
			}
		}
		lua_pop(L, 3);
	}
	pthread_mutex_unlock(&s_lua_search_lock);

	return result->len;
}

static int
console_lua_module_exec(struct console_module *module,
                        const char *entry, char **result)
{
	int err;
	const char *err_msg;
	lua_State *L = module->user_data;

	pthread_mutex_lock(&s_lua_exec_lock);

	console_lua_module_get_table(L, module); //+1
	lua_getfield(L, -1, "exec"); //+2

	lua_pushvalue(L, -2); //+3
	lua_pushstring(L, entry); //+4: second argument, the search string
	err = lua_pcall(L, 2, 1, 0); //+1:  2 argument, 1 result

	if (err != LUA_OK) {
		err_msg = lua_tostring(L, -1);
		fprintf(stderr, "%s:exec err:%s\n",
		        module->name,err_msg);
	}
	lua_pop(L, 1);

	pthread_mutex_unlock(&s_lua_exec_lock);

	return 0;
}

static void
console_lua_module_init(struct console_module *module)
{

}

static void
console_lua_module_destroy(struct console_module *module)
{

}

static inline void
console_module_assign_table(lua_State *L, struct console_module *m,
                            int table_pos)
{
	char table_name[64];
	sprintf(table_name, MODULE_TABLE_FORMAT, m->name);
	lua_pushvalue(L, table_pos);
	lua_setfield(L, LUA_REGISTRYINDEX, table_name);
}

static bool
console_module_valid_lua_search(lua_State *L, int pos)
{
	bool ret = true;
	int curr_top = lua_gettop(L);

	lua_getfield(L, pos, "search");
	if (!lua_isfunction(L, -1))
		ret = false;
	//now we do a test
	lua_pushvalue(L, pos);
	lua_pushstring(L, "");
	//2 argument, 1 result, 0 error handling
	if (lua_pcall(L, 2, 1, 0) != LUA_OK)
		ret = false;
	lua_pop(L, lua_gettop(L) - curr_top);
	return ret;
}

static bool
console_module_valid_lua_exec(lua_State *L, int pos)
{
	bool ret = true;
	int curr_top = lua_gettop(L);

	lua_getfield(L, pos, "exec");
	if (!lua_isfunction(L, -1))
		ret = false;
	//test
	lua_pushvalue(L, pos);
	lua_pushstring(L, "");
	//2 argument 1 result, 0 error
	if (lua_pcall(L, 2, 1, 0) != LUA_OK)
		ret = false;
	lua_pop(L, lua_gettop(L) - curr_top);
	return ret;
}

/**
 * @brief test for valid name and no exisinting module with same name
 */
static bool
console_module_get_name(lua_State *L, int pos, char module_name[32])
{
	bool ret = true;
	const char *name;
	char registry_name[64];

	lua_getfield(L, pos, "name");
	name = lua_tostring(L, -1);
	if (!name || strlen(name) > 31)
		ret = false;
	strcpy(module_name, name);
	lua_pop(L, 1);
	sprintf(registry_name, MODULE_UDATA_FORMAT, module_name);
	if (_LUA_ISFIELD(L, userdata, LUA_REGISTRYINDEX, registry_name))
		ret = false;
	return ret;
}

/**
 * @brief load a lua console module from table,
 *
 * this method takes a table on the pos then create a console_module_for it.
 *
 */
static int
console_module_from_lua_table(lua_State *L, int pos)
{
	struct console_module *new_module = NULL;
	char module_name[32] = {0};
	//ensure positive index
	if (pos < 0)
		pos = lua_gettop(L) + pos + 1;

	if (!_lua_istable(L, pos, MODULE_METATABLE))
		return 0;
	if (!console_module_valid_lua_search(L, pos))
		return 0;
	if (!console_module_valid_lua_exec(L, pos))
		return 0;
	if (!console_module_get_name(L, pos, module_name))
		return 0;

	new_module = lua_newuserdata(L, sizeof(struct console_module));
	memset(new_module, 0, sizeof(struct console_module));
	if (!new_module)
		return 0;
	//get module name
	strcpy((char *)new_module->name, module_name);
	new_module->user_data = L;
	new_module->search = console_lua_module_search;
	new_module->exec = console_lua_module_exec;
	new_module->init_hook = console_lua_module_init;
	new_module->destroy_hook = console_lua_module_destroy;
	// assign index does not change the stack
	console_module_assign_table(L, new_module, pos);

	return 1;
}

/**
 * @brief load a lua console module from script, as opposed to
 * console_module_from_lua_table
 *
 *
 */
static int
console_module_from_lua_file(lua_State *L, const char *module)
{
	int err = LUA_OK;
	int ret = 0;

	err = luaL_loadfile(L, module); //+1

	if (err == LUA_OK) {
		err = lua_pcall(L, 0, 1, 0); //-1 +1
		ret += 1;
	}
	if (err == LUA_OK && lua_istable(L, -1))
		ret += console_module_from_lua_table(L, -1);

	return ret;
}

/*******************************************************************************
 * module collection
 ******************************************************************************/

static void
_lua_collect_module(lua_State *L, struct console_module *module)
{
	char module_ref[64];
	int i = _lua_n_console_modules(L);

	sprintf(module_ref, MODULE_UDATA_FORMAT, module->name);
	lua_setfield(L, LUA_REGISTRYINDEX, module_ref);
	lua_getfield(L, LUA_REGISTRYINDEX, MODULES_COLLECTION);
	lua_pushstring(L, module->name);
	lua_rawseti(L, -2, i+1);
	_lua_add_console_module(L);
}

static struct console_module *
_lua_module_from_collection(lua_State *L, int i)
{
	char module_name[32];
	char module_ref[64];
	struct console_module *module = NULL;

	lua_getfield(L, LUA_REGISTRYINDEX, MODULES_COLLECTION);
	lua_rawgeti(L, -1, i+1);
	strcpy(module_name, lua_tostring(L, -1));
	lua_pop(L, 2);

	sprintf(module_ref, MODULE_UDATA_FORMAT, module_name);
	lua_getfield(L, LUA_REGISTRYINDEX, module_ref);
	module = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return module;
}

static void
_lua_clear_module_in_collection(lua_State *L, int i)
{
	char module_name[32];
	char module_ref[64];

	lua_getfield(L, LUA_REGISTRYINDEX, MODULES_COLLECTION);
	lua_rawgeti(L, -1, i+1);
	strcpy(module_name, lua_tostring(L, -1));
	lua_pop(L, 2);

	sprintf(module_ref, MODULE_UDATA_FORMAT, module_name);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, module_ref);

	sprintf(module_ref, MODULE_TABLE_FORMAT, module_name);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, module_ref);
}

/*******************************************************************************
 * LUA APIs
 ******************************************************************************/

static int
_lua_img_width(lua_State *L)
{
	struct nk_image *img =
		_lua_isudata(L, 1, MODULE_IMG_METABLE);
	if (img)
		lua_pushinteger(L, img->region[2]);
	else
		lua_pushnil(L);
	return 1;
}

static int
_lua_img_height(lua_State *L)
{
	struct nk_image *img =
		_lua_isudata(L, 1, MODULE_IMG_METABLE);
	if (img)
		lua_pushinteger(L, img->region[3]);
	else
		lua_pushnil(L);
	return 1;
}

static int
_lua_new_search_entry(lua_State *L)
{
	bool has_image = false;

	if (lua_gettop(L) < 2)
		goto nil;
	if (!_lua_istable(L, 1, MODULE_METATABLE))
		goto nil;
	if (!lua_isstring(L, 2))
		goto nil;
	if (lua_gettop(L) == 3 &&
	    _lua_isudata(L, 3, MODULE_IMG_METABLE))
		has_image = true;

	//write result;
	lua_newtable(L);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "entry");
	// get image, we maynot have it, we will just write
	if (has_image) {
		lua_pushvalue(L, 3);
		lua_setfield(L, -2, "image");
	} else {
		lua_getfield(L, LUA_REGISTRYINDEX, EMPTY_IMAGE);
		lua_setfield(L, -2, "image");
	}
	return 1;
nil:
	lua_pushnil(L);
	return 1;
}

static int
_lua_request_console_img(lua_State *L)
{
	struct nk_image *im_lua;
	const struct nk_image *im_data;
	const char *string;
	struct desktop_console *console =
		_lua_to_console(L);

	if (lua_gettop(L) != 1 ||
	    !(string = lua_tostring(L, 1)))
		lua_pushnil(L);
	else {
		im_data = desktop_console_request_image(console, string, NULL);
		if (im_data)
			lua_pushnil(L);
		else {
			im_lua = lua_newuserdata(L, sizeof(struct nk_image));
			luaL_getmetatable(L, MODULE_IMG_METABLE);
			lua_setmetatable(L, -2);
			*im_lua = *im_data;
		}
	}
	return 1;
}

static int
_lua_load_module(lua_State *L)
{
	struct console_module *module;
	int ret = 0;

	if (lua_gettop(L) != 1)
		return luaL_error(L, "load_module: invalid args\n");
	else if (lua_isstring(L, 1))
		ret = console_module_from_lua_file(L, lua_tostring(L, 1));
	else if (_lua_istable(L, 1, MODULE_METATABLE))
		ret = console_module_from_lua_table(L, 1);
	else
		return luaL_error(L, "load_module: invalid args\n");

	//ret can be 1 or 2, depends on how module is created
	if (ret > 0) {
		module = lua_touserdata(L, -1);
		_lua_collect_module(L, module);
		return 0;
	}

	return luaL_error(L, "load_module: invalid module\n");;
}

 static int
_lua_load_builtin_module(lua_State *L)
{
	const char *name;
	static struct {
		struct console_module *module;
		const char *name;
	} builtin_modules[] = {
		{.module = &app_module, .name = "APP"},
		{.module = &cmd_module, .name = "CMD"},
	};
	struct console_module *module;

	if (lua_gettop(L) != 1 ||
	    !(name = lua_tostring(L, 1)))
		return luaL_error(L, "%s: invalid args\n",
		                  "load_builtin_module");
	for (int i = 0; i < 2; i++) {
		if (!strcasecmp(builtin_modules[i].name, name)) {
			//we will use lua GC for maintaining the modules
			module = lua_newuserdata(L, sizeof(struct console_module));
			memcpy(module, builtin_modules[i].module,
			       sizeof(struct console_module));
			_lua_collect_module(L, module);
			return 0;
		}
	}

	return luaL_error(L, "%s: invalid builtin module %s\n",
	                  "load_builtin_module", name);
}

static int
_lua_request_module_metatable(lua_State *L)
{
	luaL_getmetatable(L, MODULE_METATABLE);
	return 1;
}

/* this function cannot crash */
static int
_lua_console_load_images(lua_State *L)
{
	struct wl_array handle_array, string_array;
	off_t *offset;
	char *string;
	const char *tocpy;
	struct desktop_console *console = _lua_to_console(L);

	wl_array_init(&handle_array);
	wl_array_init(&string_array);

	if (lua_gettop(L) != 1 || !lua_istable(L, 1)) {
		lua_pushboolean(L, false);
		return 1;
	}
	for (size_t i = 0; i < lua_rawlen(L, 1); i++) {
		lua_rawgeti(L, -1, i+1);
		tocpy = lua_tostring(L, -1);
		if (lua_isstring(L, -1) && is_file_exist(tocpy) &&
		    is_file_type(tocpy, ".png")) {
			string = wl_array_add(&string_array, strlen(tocpy)+1);
			offset = wl_array_add(&handle_array, sizeof(off_t));
			strcpy(string, tocpy);
			*offset = (tocpy - (const char *)string_array.data);
		}
		lua_pop(L, 1);
	}
	if (handle_array.size && string_array.size)
		desktop_console_load_icons(console,
		                           &handle_array, &string_array);
	wl_array_release(&handle_array);
	wl_array_release(&string_array);

	lua_pushboolean(L, true);
	return 1;
}

static int
luaopen_taiwins_console(lua_State *L)
{
	static const luaL_Reg lib[] = {
		{"rquest_image", _lua_request_console_img},
		{"load_module", _lua_load_module},
		{"load_builtin_module", _lua_load_builtin_module},
		{"module_base", _lua_request_module_metatable},
		{"load_images", _lua_console_load_images},
	};
	luaL_newlib(L, lib);
	return 1;
}

static void
_lua_register_metatables(lua_State *L, struct desktop_console *console)
{
	struct nk_image *empty_img;

	lua_pushlightuserdata(L, console);
	lua_setfield(L, LUA_REGISTRYINDEX, CONSOLE);

	//moudle count
	lua_pushinteger(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, MODULES_COUNT);

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, MODULES_COLLECTION);

	//metatable for module
	luaL_newmetatable(L, MODULE_METATABLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	_LUA_REGISTER("new_result", _lua_new_search_entry);
	lua_pop(L, 1);

	//metatable for image, also create an empty image
	empty_img = lua_newuserdata(L, sizeof(struct nk_image));
	memset(empty_img, 0, sizeof(struct nk_image));
	luaL_newmetatable(L, MODULE_IMG_METABLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	_LUA_REGISTER("width", _lua_img_width);
	_LUA_REGISTER("height", _lua_img_height);
	lua_setmetatable(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, EMPTY_IMAGE);
}

bool
desktop_console_run_config_lua(struct desktop_console *console,
                               const char *path)
{
	const char *err_msg;
	lua_State *L;
	int n;
	struct console_module *module;

	//for all the modules inside the folder. lets create module from that
	pthread_mutex_init(&s_lua_search_lock, NULL);
	pthread_mutex_init(&s_lua_exec_lock, NULL);
	s_L = luaL_newstate();
	luaL_openlibs(s_L);
	L = s_L;

	_lua_register_metatables(L, console);
	luaL_requiref(L, "taiwins_console",
	              luaopen_taiwins_console, true);

	if (luaL_dofile(L, path)) {
		n = _lua_n_console_modules(L);
		err_msg = lua_tostring(L, -1);
		fprintf(stderr,"ERROR occured: %s\n", err_msg);
		//clean up the modules
		for (int i = 0; i < n; i++)
			_lua_clear_module_in_collection(L, i);
		return false;
	} else {
		n = _lua_n_console_modules(L);

		lua_getfield(L, LUA_REGISTRYINDEX, MODULES_COLLECTION);
		for (int i = 0; i < n; i++) {
			module = _lua_module_from_collection(L, i);
			desktop_console_append_module(console, module);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	return true;
}

void
desktop_console_release_lua_config(struct desktop_console *console)
{
	pthread_mutex_destroy(&s_lua_search_lock);
	pthread_mutex_destroy(&s_lua_exec_lock);

	lua_close(s_L);
}
