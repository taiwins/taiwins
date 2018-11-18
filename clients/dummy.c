#include <stdio.h>
#include <wayland-client.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "ui.h"
#include "client.h"
#include "nuklear/nk_wl_egl.h"
#include "../config.h"


//create a egl ui

static struct application {
	struct wl_shell *shell;
	struct wl_globals global;
	struct app_surface surface;
	struct nk_egl_backend *bkend;
	struct wl_shell_surface *shell_surface;
	struct egl_env env;
	bool done;
} App;




static void
global_registry_handler(void *data,
			struct wl_registry *registry, uint32_t id,
			const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_shell_interface.name) == 0) {
		App.shell = wl_registry_bind(registry, id, &wl_shell_interface, version);
		fprintf(stdout, "wl_shell %d announced\n", id);
	} else
		wl_globals_announce(&App.global, registry, id, interface, version);
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


static void
sample_widget(struct nk_context *ctx, float width, float height, struct app_surface *data)
{
	struct application *app = &App;
	enum nk_buttons btn;
	uint32_t sx, sy;
	//TODO, change the draw function to app->draw_widget(app);
	enum {EASY, HARD};
	static int op = EASY;
	static struct nk_text_edit text_edit;
	static bool init_text_edit = false;
	static char text_buffer[256];
	if (!init_text_edit) {
		init_text_edit = true;
		nk_textedit_init_fixed(&text_edit, text_buffer, 256);
	}

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, 30, 2);
	if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
	if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;

	nk_layout_row_dynamic(ctx, 25, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &text_edit, nk_filter_default);
	if (nk_egl_get_keyinput(ctx) == XKB_KEY_Escape)
		app->done = true;

	bool ret = nk_egl_get_btn(ctx, &btn, &sx, &sy);
	if (ret && btn == NK_BUTTON_MIDDLE) {
		wl_shell_surface_resize(App.shell_surface, App.global.inputs.wl_seat,
					App.global.inputs.serial,
					WL_SHELL_SURFACE_RESIZE_BOTTOM);
	}
}

static
void
handle_ping(void *data,
		     struct wl_shell_surface *wl_shell_surface,
		     uint32_t serial)
{
	fprintf(stderr, "ping!!!\n");
	wl_shell_surface_pong(wl_shell_surface, serial);
}


static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
		 uint32_t edges, int32_t width, int32_t height)
{
	fprintf(stderr, "shell_surface has configure: %d, %d, %d\n", edges, width, height);
	nk_egl_resize(App.bkend, width, height);
}

struct wl_shell_surface_listener pingpong = {
	.ping = handle_ping,
	.configure = handle_configure,
	.popup_done = handle_popup_done
};



/* okay we are gonna try to set the theme for nuklear */

int main(int argc, char *argv[])
{
	/* const char *wayland_socket = getenv("WAYLAND_SOCKET"); */
	/* const int fd = atoi(wayland_socket); */
	/* FILE *log = fopen("/tmp/client-log", "w"); */
	/* if (!log) */
	/*	return -1; */
	/* fprintf(log, "%d\n", fd); */
	/* fflush(log); */
	/* fclose(log); */

	struct wl_display *wl_display = wl_display_connect(NULL);
	if (!wl_display)
		fprintf(stderr, "okay, I don't even have wayland display\n");
	wl_globals_init(&App.global, wl_display);
	App.global.theme = taiwins_dark_theme;

	struct wl_registry *registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	App.done = false;

	wl_display_dispatch(wl_display);
	wl_display_roundtrip(wl_display);

	struct wl_surface *wl_surface = wl_compositor_create_surface(App.global.compositor);
	struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(App.shell, wl_surface);
	app_surface_init(&App.surface, wl_surface, (struct wl_proxy *)shell_surface);
	App.surface.wl_globals = &App.global;

	wl_shell_surface_add_listener(shell_surface, &pingpong, NULL);
	wl_shell_surface_set_toplevel(shell_surface);
	App.shell_surface = shell_surface;

	egl_env_init(&App.env, App.global.display);
	App.surface.w = 200;
	App.surface.h = 400;
	App.surface.s = 1;

	App.bkend = nk_egl_create_backend(&App.env);
	nk_egl_impl_app_surface(&App.surface, App.bkend, sample_widget, 200, 400, 0, 0);
	app_surface_frame(&App.surface, false);

	fprintf(stdout, "here\n");
	while (wl_display_dispatch(wl_display) != -1 && !App.done)
		;
	nk_egl_destroy_backend(App.bkend);
	egl_env_end(&App.env);
	wl_shell_surface_destroy(shell_surface);
	app_surface_release(&App.surface);
	wl_globals_release(&App.global);
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);
	return 0;
}
