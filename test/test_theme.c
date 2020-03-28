#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <client.h>
#include <ui.h>
#include <theme.h>
#include <nk_backends.h>


static struct application {
	struct wl_shell *shell;
	struct tw_globals global;
	struct tw_appsurf surface;
	struct nk_wl_backend *bkend;
	struct wl_shell_surface *shell_surface;
	bool done;
	struct nk_image image;
	struct nk_user_font *user_font;

	struct tw_theme theme;

} App;


/*******************************************************************************
 * registery
 ******************************************************************************/

static void
global_registry_handler(void *data,
			struct wl_registry *registry, uint32_t id,
			const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_shell_interface.name) == 0) {
		App.shell = wl_registry_bind(registry, id, &wl_shell_interface, version);
		fprintf(stdout, "wl_shell %d announced\n", id);
	} else
		tw_globals_announce(&App.global, registry, id, interface, version);
}


static void
global_registry_removal(void *data, struct wl_registry *registry, uint32_t id)
{
	fprintf(stdout, "wl_registry removes global! %d\n", id);
}

static struct wl_registry_listener registry_listener = {
	.global = global_registry_handler,
	.global_remove = global_registry_removal,
};


/*******************************************************************************
 * theme read
 ******************************************************************************/


void tw_theme_init_for_lua(struct tw_theme *theme, lua_State *L);

static void
read_theme(lua_State *L, struct application *app, const char *script)
{
	tw_theme_init_for_lua(&app->theme, L);

	if (luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0))
		fprintf(stderr, "err_msg: %s\n", lua_tostring(L, -1));

	lua_close(L);


}

int main(int argc, char *argv[])
{
	lua_State *L;
	struct wl_display *wl_display = wl_display_connect(NULL);

	if (!wl_display) {
		fprintf(stderr, "no display available.");
		return 0;
	}
	tw_globals_init(&App.global, wl_display);


	struct wl_registry *registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(wl_display);
	wl_display_roundtrip(wl_display);

	L = luaL_newstate();
	if (!L)
		goto err_lua;
	read_theme(L, &App, argv[1]);

	struct wl_surface *wl_surface = wl_compositor_create_surface(App.global.compositor);
	struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(App.shell, wl_surface);

	nk_wl_impl_wl_shell_surface(&App.surface, shell_surface);
	wl_shell_surface_set_toplevel(shell_surface);
	App.shell_surface = shell_surface;
	App.surface.known_mimes[TW_MIME_TYPE_TEXT] = "text/";
	App.bkend = nk_cairo_create_bkend();





err_lua:

	return 0;
}
