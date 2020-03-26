#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <wayland-server.h>
#include <wayland-util.h>
#include <strops.h>
#include <sequential.h>
#include <theme.h>

#include "../server/config.h"
#include "../server/lua_helper.h"

static bool
dummpy_apply(struct tw_config *c, struct tw_option_listener *l)
{
	fprintf(stderr, "applying configuration\n");
	return true;
}


struct tw_option_listener option = {
	.type = TW_OPTION_RGB,
	.apply = dummpy_apply,
};

extern int tw_theme_read(lua_State *L);

/*****************************************************************
 * LUA_BINDINGS
 ****************************************************************/

static inline struct wl_array
tw_menu_to_wl_array(const struct tw_menu_item *items, const int len) {
	struct wl_array serialized;
	serialized.alloc = sizeof(struct tw_menu_item) * len;
	serialized.size = serialized.alloc;
	serialized.data = (void *)items;
	return serialized;
}

/* whether this is a menu item */
static bool
_lua_is_menu_item(struct lua_State *L, int idx)
{
	if (lua_rawlen(L, idx) != 2)
		return false;
	size_t len[2] = {TAIWINS_MAX_MENU_ITEM_NAME,
	                 TAIWINS_MAX_MENU_CMD_LEN};
	for (int i = 0; i < 2; ++i) {
		lua_rawgeti(L, idx, i+1);
		const char *value = (lua_type(L, -1) == LUA_TSTRING) ?
			lua_tostring(L, -1) : NULL;
		if (value == NULL || strlen(value) >= (len[i]-1)) {
			lua_pop(L, 1);
			return false;
		}
		lua_pop(L, 1);
	}
	return true;
}

static bool
_lua_parse_menu(struct lua_State *L, vector_t *menus)
{
	bool parsed = true;
	struct tw_menu_item menu_item = {
		.has_submenu = false,
		.len = 0};
	if (_lua_is_menu_item(L, -1)) {
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		strop_ncpy(menu_item.endnode.title, lua_tostring(L, -2),
			TAIWINS_MAX_MENU_ITEM_NAME);
		strop_ncpy(menu_item.endnode.cmd, lua_tostring(L, -1),
			TAIWINS_MAX_MENU_CMD_LEN);
		lua_pop(L, 2);
		vector_append(menus, &menu_item);
	} else if (lua_istable(L, -1)) {
		int n = lua_rawlen(L, -1);
		int currlen = menus->len;
		for (int i = 1; i <= n && parsed; i++) {
			lua_rawgeti(L, -1, i);
			parsed = parsed && _lua_parse_menu(L, menus);
			lua_pop(L, 1);
		}
		if (parsed) {
			menu_item.has_submenu = true;
			menu_item.len = menus->len - currlen;
			vector_append(menus, &menu_item);
		}
	} else
		return false;
	return parsed;
}

static int
_lua_set_menus(lua_State *L)
{
	vector_t menus;
	vector_init_zero(&menus, sizeof(struct tw_menu_item), NULL);
	tw_lua_stackcheck(L, 2);
	luaL_checktype(L, 2, LUA_TTABLE);
	//once you have a heap allocated data, calling luaL_error afterwards
	//causes leaks
	if (!_lua_parse_menu(L, &menus)) {
		vector_destroy(&menus);
		return luaL_error(L, "error parsing menus.");
	}
        struct wl_array serialized =
            tw_menu_to_wl_array(menus.elems, menus.len);
        (void)serialized;
	vector_destroy(&menus);
	return 0;
}


static int
dummy_lua_method_0(lua_State *L)
{
	fprintf(stderr, "dummy method 0 called\n");
	return 0;
}

static int
dummy_table__index(lua_State *L)
{
	const char *param = lua_tostring(L, 2);
	if (strcmp(param, "haha") == 0) {
		lua_pushstring(L, "papapa");
		return 1;
	}
	return luaL_error(L, "invalid table index %s", param);
}

static int
dummy_table__newindex(lua_State *L)
{
	const char *param = lua_tostring(L, 2);
	printf("param is %s\n", param);
	return 0;
}

static int
get_dummy_table(lua_State *L)
{
	lua_newtable(L); //1
	luaL_getmetatable(L, "metatable_dummy"); //3
	lua_setmetatable(L, -2); //2

	for (int i = 0; i < 10; i++) {
		lua_newtable(L); //2

		luaL_getmetatable(L, "metatable_dummy"); //3
		lua_setmetatable(L, -2); //2

		/* lua_pushstring(L, "dummy_field"); //3 */
		/* lua_pushstring(L, "haha"); //4 */
		/* lua_settable(L, -3); //2 */

		lua_rawseti(L, -2, i+1);
	}
	return 1;
}

static int
lua_get_dummy_interface(lua_State *L)
{
	lua_newtable(L);
	//install methods before assigning metatables.
	REGISTER_METHOD(L, "get_dummy_table", get_dummy_table);
	//since we override index and newindex, the metatable only takes care of
	//element access and assigning right now.
	luaL_getmetatable(L, "metatable_dummy");
	lua_setmetatable(L, -2);
	return 1;
}

static bool
lua_component_init(struct tw_config *config, lua_State *L,
		   struct tw_config_component_listener *listener)
{

	luaL_newmetatable(L, "metatable_dummy");
	lua_pushcfunction(L, dummy_table__index);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction(L, dummy_table__newindex);
	lua_setfield(L, -2, "__newindex");


	REGISTER_METHOD(L, "dummy_method", dummy_lua_method_0);
	lua_pop(L, 1);

	REGISTER_METHOD(L, "get_dummy_interface", lua_get_dummy_interface);
	REGISTER_METHOD(L, "set_menus", _lua_set_menus);

	struct tw_theme *theme = lua_newuserdata(L, sizeof(struct tw_theme));
	tw_theme_init_default(theme);
	lua_setfield(L, LUA_REGISTRYINDEX, "tw_theme");
	REGISTER_METHOD(L, "read_theme", tw_theme_read);
	return true;
}

struct tw_config_component_listener lua_component = {
	.link = {
		&lua_component.link, &lua_component.link,},
	.init = lua_component_init,
};

int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_log_context *context = weston_log_ctx_compositor_create();
	struct weston_compositor *ec = weston_compositor_create(display, context, NULL);
	struct tw_config *config = tw_config_create(ec, vprintf);

	tw_config_add_option_listener(config, "bigmac", &option);
	tw_config_add_component(config, &lua_component);

	tw_config_run(config, argv[1]);

	tw_config_destroy(config);
	weston_compositor_tear_down(ec);
	weston_log_ctx_compositor_destroy(ec);
	weston_compositor_destroy(ec);
	wl_display_terminate(display);
	wl_display_destroy(display);
	return 0;
}
