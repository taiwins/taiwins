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
 * sample widgets
 ******************************************************************************/

void
sample_widget(struct nk_context *ctx, float width, float height,
              struct tw_appsurf *data)
{
	enum {EASY, HARD};
	static struct nk_text_edit text_edit;
	static bool init_text_edit = false;
	static char text_buffer[256];
	static int op = EASY;
	int unicodes[] = {0xf1c1, 'C', 0xf1c2, 0xf1c3, 0xf1c4, 0xf1c5};
	char strings[256];
	int count = 0;

	nk_style_set_font(ctx, App.user_font);

	if (!init_text_edit) {
		init_text_edit = true;
		nk_textedit_init_fixed(&text_edit, text_buffer, 256);
	}

	for (int i = 0; i < 5; i++) {
		count += nk_utf_encode(unicodes[i], strings+count, 256-count);
	}
	strings[count] = '\0';

	if (nk_begin(ctx, "demo0", nk_rect(0,0, width/2, height/2),
		     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
		nk_layout_row_dynamic(ctx, 30, 1);
		nk_edit_buffer(ctx, NK_EDIT_FIELD, &text_edit, nk_filter_default);

	} nk_end(ctx);

	if (nk_begin(ctx, "demo1", nk_rect(width/2, 0, width/2, height/2),
		     NK_WINDOW_BORDER)) {
		nk_layout_row_dynamic(ctx, 30, 2);
		if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
		if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;
		nk_layout_row_dynamic(ctx, 30, 1);
		if (nk_button_label(ctx, strings)) {
			App.global.event_queue.quit = true;
		}
	} nk_end(ctx);

	if (nk_begin(ctx, "demo2", nk_rect(0, height/2, width, height/2),
		     NK_WINDOW_BORDER)) {
		nk_layout_row_dynamic(ctx, 40, 1);
		//one way data-binding
		nk_label(ctx, op == EASY ? "easy" : "hard", NK_TEXT_ALIGN_LEFT);

	} nk_end(ctx);
}


/*******************************************************************************
 * theme read
 ******************************************************************************/


void tw_theme_init_for_lua(struct tw_theme *theme, lua_State *L);

static void
read_theme(lua_State *L, struct application *app, const char *script)
{
	lua_newtable(L);
	tw_theme_init_for_lua(&app->theme, L);
	lua_getfield(L, -1, "read_theme");
	if (!lua_isnil(L, -1) && lua_isfunction(L, -1))
		lua_setglobal(L, "read_theme");

	if (luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0))
		fprintf(stderr, "err_msg: %s\n", lua_tostring(L, -1));

	lua_close(L);

	App.global.theme = &app->theme;
}

int main(int argc, char *argv[])
{
        const struct nk_wl_font_config config = {
	        .name = "icons",
	        .slant = NK_WL_SLANT_ROMAN,
	        .pix_size = 16,
	        .scale = 1,
	        .TTFonly = true,
	        .nranges = 0,
	        .ranges = NULL,
        };

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
	struct wl_shell_surface *shell_surface =
		wl_shell_get_shell_surface(App.shell, wl_surface);

	tw_appsurf_init(&App.surface, wl_surface,
			 &App.global, TW_APPSURF_APP,
			 TW_APPSURF_COMPOSITE);

	nk_wl_impl_wl_shell_surface(&App.surface, shell_surface);
	wl_shell_surface_set_toplevel(shell_surface);
	App.shell_surface = shell_surface;
	App.surface.known_mimes[TW_MIME_TYPE_TEXT] = "text/";
	App.bkend = nk_cairo_create_bkend();
	App.user_font = nk_wl_new_font(config, App.bkend);

	nk_cairo_impl_app_surface(&App.surface, App.bkend, sample_widget,
	                          tw_make_bbox_origin(200, 400, 2));
	tw_appsurf_frame(&App.surface, false);
	tw_globals_dispatch_event_queue(&App.global);

	wl_shell_surface_destroy(shell_surface);
	tw_appsurf_release(&App.surface);
	nk_cairo_destroy_bkend(App.bkend);

	tw_globals_release(&App.global);
	tw_theme_fini(&App.theme);
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);

err_lua:

	return 0;
}
