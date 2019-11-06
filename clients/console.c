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


//well, you could usually find icons in /usr/share/icons/hicolor/, which has a tons of icons
//and you can generate caches for all those icons, need q quick way to compute hash though.
struct completion_item {
	struct nk_image icon;
	char text[256];
};

static const char *tmp_tab_chars[5] = {
	"aaaaaa",
	"bbbbbb",
	"cccccc",
	"dddddd",
	"eeeeee",
};


/**
 * @brief get the next
 */
static const char *
auto_complete(struct desktop_console *console)
{
	//we have some shadowed context here
	static int i = 0;
	return tmp_tab_chars[i++ % 5];
}

static void
submit_console(struct app_surface *surf)
{
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
	tw_console_submit(console->interface, console->decision_buffer, console->exec_id);
	tw_ui_destroy(console->proxy);
	console->proxy = NULL;
	app_surface_release(&console->surface);
}


/**
 * @brief a more serious console implementation
 */
static void
draw_console(struct nk_context *ctx, float width, float height,
	      struct app_surface *surf)
{
	//TODO change the state machine
	enum EDITSTATE {NORMAL, COMPLETING, SUBMITTING};
	static enum EDITSTATE edit_state = NORMAL;
	static char previous_tab[256] = {0};

	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);

	nk_layout_row_static(ctx, height - 30, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &console->text_edit, nk_filter_default);
	//we could go into two different state, first is compeletion, then it is submission
	if (nk_wl_get_keyinput(ctx) == XKB_KEY_NoSymbol) //key up
		return;
	if (nk_wl_get_keyinput(ctx) == XKB_KEY_Tab)
		edit_state = COMPLETING;
	else if (nk_wl_get_keyinput(ctx) == XKB_KEY_Return)
		edit_state = SUBMITTING;
	else
		edit_state = NORMAL;

	switch (edit_state) {
	case COMPLETING:
		nk_textedit_delete(&console->text_edit, console->text_edit.cursor - strlen(previous_tab),
				   strlen(previous_tab));
		strcpy(previous_tab, auto_complete(console));
		nk_textedit_text(&console->text_edit, previous_tab, strlen(previous_tab));
		break;
	case SUBMITTING:
		memset(previous_tab, 0, sizeof(previous_tab));
		edit_state = NORMAL;
		nk_wl_add_idle(ctx, submit_console);
		break;
	case NORMAL:
		memset(previous_tab, 0, sizeof(previous_tab));
		break;
	}
	//TODO, have a close option, thus close without submiting
}

//fuck, I wish that I have c++
static void
update_app_config(void *data,
		  struct tw_console *tw_console,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{
//we don't nothing here now
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
	console->rax = raxNew();

	console->bkend = nk_cairo_create_bkend();
	nk_textedit_init_fixed(&console->text_edit, console->chars, 256);
	vector_init_zero(&console->completions,
			 sizeof(struct completion_item), NULL);
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
	raxFree(console->rax);

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
