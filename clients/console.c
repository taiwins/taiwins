#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-client.h>
#include <wayland-taiwins-desktop-client-protocol.h>
#include <os/exec.h>

#include <client.h>
#include <ui.h>
#include <rax.h>
#include <nk_backends.h>
#include "../shared_config.h"

#include "common.h"
#include "console.h"
#include "vector.h"


//well, you could usually find icons in /usr/share/icons/hicolor/, which has a tons of icons
//and you can generate caches for all those icons, need q quick way to compute hash though.
struct completion_item {
	struct nk_image icon;
	char text[256];
};


static void
submit_console(struct app_surface *surf)
{
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
	tw_console_submit(console->interface, console->decision_buffer, console->exec_id);
	//and also do the exec.
	tw_ui_destroy(console->proxy);
	console->proxy = NULL;
	app_surface_release(&console->surface);
}

static void
issue_commands(struct desktop_console *console,
	       const struct nk_str *str)
{
	struct console_module *module = NULL;
	char command[256] = {0};

	strncpy(command, (char *)str->buffer.memory.ptr,
		MIN(255, str->len));
	vector_for_each(module, &console->modules)
		console_module_command(module, command, NULL);
}

static void
search_res_header(struct nk_context *ctx, const char *title)
{
    /* nk_style_set_font(ctx, &media->font_18->handle); */
    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, title, NK_TEXT_LEFT);
}

static int
search_res_widget(struct nk_context *ctx, float height,
		  const console_search_entry_t *entry)
{
    static const float ratio[] = {0.15f, 0.85f};
    const char *str = search_entry_get_string(entry);
    /* nk_style_set_font(ctx, &media->font_22->handle); */
    nk_layout_row(ctx, NK_DYNAMIC, height, 2, ratio);
    nk_spacing(ctx, 1);
    return nk_button_label(ctx, str);
}

static void
draw_search_results(struct nk_context *ctx,
		    struct desktop_console *console)
{
	//now we checking the results, the results could be available in the
	//keyup state. The events triggling could be a timer event or idle event, but anyway
	nk_layout_row_dynamic(ctx, 200, 1);
	nk_group_begin(ctx, "search_result", NK_WINDOW_SCALABLE);
	for (int i = 0; i < console->modules.len; i++) {
		vector_t *result =
			vector_at(&console->search_results, i);
		struct console_module *module =
			vector_at(&console->modules, i);
		//update the results if there is new stuff
		int errcode = console_module_take_search_result(module, result);
		if (errcode || !result->len)
			continue;
		console_search_entry_t *entry = NULL;

		search_res_header(ctx, module->name);
		vector_for_each(entry, result) {
			search_res_widget(ctx, 30, entry);
		}
	}
	nk_group_end(ctx);
}

/**
 * @brief nuklear draw calls
 */
static void
draw_console(struct nk_context *ctx, float width, float height,
	     struct app_surface *surf)
{
	//TODO change the state machine
	enum EDITSTATE {NORMAL, SUBMITTING};
	static enum EDITSTATE edit_state = NORMAL;
	static char previous_tab[256] = {0};

	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);

	nk_layout_row_static(ctx, 30, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &console->text_edit, nk_filter_default);
	//issue commands only in key done state
	if (nk_wl_get_keyinput(ctx) != XKB_KEY_NoSymbol)
		issue_commands(console, &console->text_edit.string);
	draw_search_results(ctx, console);

	if (nk_wl_get_keyinput(ctx) == XKB_KEY_NoSymbol) //key up
		return;
	if (nk_wl_get_keyinput(ctx) == XKB_KEY_Return)
		edit_state = SUBMITTING;
	else
		edit_state = NORMAL;

	switch (edit_state) {
	case SUBMITTING:
		memset(previous_tab, 0, sizeof(previous_tab));
		edit_state = NORMAL;
		nk_wl_add_idle(ctx, submit_console);
		break;
	case NORMAL:
		memset(previous_tab, 0, sizeof(previous_tab));
		break;
	}
}

static void
update_app_config(void *data,
		  struct tw_console *tw_console,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{

}

static void
start_console(void *data, struct tw_console *tw_console,
	      wl_fixed_t width, wl_fixed_t height, wl_fixed_t scale)
{
	struct desktop_console *console = (struct desktop_console *)data;
	struct app_surface *surface = &console->surface;
	struct wl_surface *wl_surface = NULL;
	int w = wl_fixed_to_int(width);
	int h = wl_fixed_to_int(height);

	wl_surface = wl_compositor_create_surface(console->globals.compositor);
	console->proxy = tw_console_launch(tw_console, wl_surface);

	app_surface_init(surface, wl_surface,
			 &console->globals, APP_SURFACE_WIDGET,
			 APP_SURFACE_NORESIZABLE);
	surface->wl_globals = &console->globals;
	nk_cairo_impl_app_surface(surface, console->bkend, draw_console,
				make_bbox_origin(w, h, 1));
	app_surface_frame(surface, false);
}

static void
exec_application(void *data, struct tw_console *console, uint32_t id)
{
	struct desktop_console *desktop_console = data;
	if (!strlen(desktop_console->chars))
		return;
	char *const forks[] = {desktop_console->chars, NULL};

	if (id != desktop_console->exec_id) {
		fprintf(stderr, "exec order not consistant, something wrong.");
	} else {
		fprintf(stderr, "creating weston terminal");
		//parsing the input and command buffer. Then do it
		fork_exec(1, forks);
	}
	nk_textedit_init_fixed(&desktop_console->text_edit, desktop_console->chars, 256);

	desktop_console->exec_id++;
}

struct tw_console_listener console_impl = {
	.application_configure = update_app_config,
	.start = start_console,
	.exec = exec_application,
};

static void
console_release_module(void *m)
{
	struct console_module *module = m;
	console_module_release(module);
}

static void
console_free_search_results(void *m)
{
	vector_t *v = m;
	vector_destroy(v);
}


static void
init_console(struct desktop_console *console)
{
	memset(console->chars, 0, sizeof(console->chars));
	console->quit = false;
	shm_pool_init(&console->pool, console->globals.shm,
		      TW_CONSOLE_CONF_NUM_DECISIONS * sizeof(struct taiwins_decision_key),
		      console->globals.buffer_format);
	console->decision_buffer = shm_pool_alloc_buffer(&console->pool,
							  sizeof(struct taiwins_decision_key),
							  TW_CONSOLE_CONF_NUM_DECISIONS);
	console->bkend = nk_cairo_create_bkend();
	nk_textedit_init_fixed(&console->text_edit, console->chars, 256);
	vector_init_zero(&console->completions,
			 sizeof(struct completion_item), NULL);

	struct console_module *module;
	vector_t empty_res = {0};
	// init modules
	vector_init(&console->modules, sizeof(struct console_module),
		    console_release_module);
	//adding modules
	vector_append(&console->modules, &cmd_module);
	vector_for_each(module, &console->modules)
		console_module_init(module, console); //thread created

	vector_init(&console->search_results, sizeof(vector_t),
		    console_free_search_results);
	vector_for_each(module, &console->modules)
		vector_append(&console->search_results, &empty_res);
}

static void
end_console(struct desktop_console *console)
{
	nk_textedit_free(&console->text_edit);
	nk_cairo_destroy_bkend(console->bkend);
	shm_pool_release(&console->pool);

	tw_console_destroy(console->interface);
	wl_globals_release(&console->globals);

	vector_destroy(&console->completions);
	vector_destroy(&console->modules);
	vector_destroy(&console->search_results);

	console->quit = true;
}

static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_console *console = (struct desktop_console *)data;

	if (strcmp(interface, tw_console_interface.name) == 0) {
		fprintf(stderr, "console registé\n");
		console->interface = (struct tw_console *)
			wl_registry_bind(wl_registry, name, &tw_console_interface, version);
		tw_console_add_listener(console->interface, &console_impl, console);
	} else
		wl_globals_announce(&console->globals, wl_registry, name, interface, version);
}


static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


int
main(int argc, char *argv[])
{
	if (!create_cache_dir())
		return -1;

	struct desktop_console tw_console;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	wl_globals_init(&tw_console.globals, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &tw_console);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	init_console(&tw_console);
	tw_console.globals.theme = taiwins_dark_theme;

	//okay, now we should create the buffers
	//event loop
	while(wl_display_dispatch(display) != -1 && !tw_console.quit);
	end_console(&tw_console);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
