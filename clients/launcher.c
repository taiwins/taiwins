#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cairo/cairo.h>

#include <wayland-client.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include "client.h"
#include "ui.h"
#include "nk_wl_egl.h"


/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE TAIWINS_LAUNCHER_CONF_STRIDE
#define NUM_DECISIONS TAIWINS_LAUNCHER_CONF_NUM_DECISIONS

//every decision represents a row in wl_buffer, we need to make it as aligned as possible
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));


struct desktop_launcher {
	struct taiwins_launcher *interface;
	struct wl_globals globals;
	struct app_surface surface;
	struct wl_buffer *decision_buffer;

	off_t cursor;
	char chars[256];
	bool quit;
	//for nuklear
	struct nk_egl_backend *bkend;
	struct egl_env env;
	//a good hack is that this text_edit is stateless, we don't need to
	//store anything once submitted
	struct nk_text_edit text_edit;
	const char *previous_tab;
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
auto_complete(struct desktop_launcher *launcher)
{
	//we have some shadowed context here
	static int i = 0;
	return tmp_tab_chars[i++ % 5];
}


/**
 * @brief a more serious launcher implementation
 */
static void
draw_launcher(struct nk_context *ctx, float width, float height, void *data)
{
	//uses a state machine
	enum EDITSTATE {NORMAL, COMPLETING, SUBMITTING};
	static enum EDITSTATE edit_state = NORMAL;
	static char previous_tab[256] = {0};

	struct desktop_launcher *launcher = data;

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);

	nk_layout_row_static(ctx, height - 30, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &launcher->text_edit, nk_filter_default);
	//we could go into two different state, first is compeletion, then it is submission
	if (nk_egl_get_keyinput(ctx) == XKB_KEY_Tab)
		edit_state = COMPLETING;
	else if (nk_egl_get_keyinput(ctx) == XKB_KEY_Return)
		edit_state = SUBMITTING;
	else
		edit_state = NORMAL;

	switch (edit_state) {
	case COMPLETING:
		for (int i = 0; i < strlen(previous_tab); i++) {
			nk_textedit_undo(&launcher->text_edit);
		}
		fprintf(stderr, "%s\n", launcher->chars);
		strcpy(previous_tab, auto_complete(launcher));
		nk_textedit_text(&launcher->text_edit, previous_tab, strlen(previous_tab));
		break;
	case SUBMITTING:
		memset(previous_tab, 0, sizeof(previous_tab));
		edit_state = NORMAL;
		nk_textedit_init_fixed(&launcher->text_edit, launcher->chars, 256);
		//we should skip the the rendering here, how?
		taiwins_launcher_submit(launcher->interface, launcher->decision_buffer);
		break;
	case NORMAL:
		memset(previous_tab, 0, sizeof(previous_tab));
		break;
	}
}

//fuck, I wish that I have c++
static void
update_app_config(void *data,
		  struct taiwins_launcher *taiwins_launcher,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{
//we don't nothing here now
}

static void
start_launcher(void *data,
	       struct taiwins_launcher *taiwins_launcher,
	       wl_fixed_t width,
	       wl_fixed_t height,
	       wl_fixed_t scale)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;
	//yeah, generally you will want a buffer from this
	taiwins_launcher_set_launcher(launcher->interface, launcher->surface.wl_surface);
	nk_egl_launch(launcher->bkend,
		      wl_fixed_to_int(width),
		      wl_fixed_to_int(height),
		      wl_fixed_to_double(scale), draw_launcher, launcher);
}


struct taiwins_launcher_listener launcher_impl = {
	.application_configure = update_app_config,
	.start = start_launcher,
};




static void
init_launcher(struct desktop_launcher *launcher)
{
	memset(launcher->chars, 0, sizeof(launcher->chars));
	launcher->quit = false;
	appsurface_init(&launcher->surface, NULL, APP_WIDGET,
			launcher->globals.compositor, NULL);
	egl_env_init(&launcher->env, launcher->globals.display);
	launcher->bkend = nk_egl_create_backend(&launcher->env,
						launcher->surface.wl_surface);
	nk_textedit_init_fixed(&launcher->text_edit, launcher->chars, 256);
}


static void
release_launcher(struct desktop_launcher *launcher)
{
	nk_textedit_free(&launcher->text_edit);
	nk_egl_destroy_backend(launcher->bkend);
	egl_env_end(&launcher->env);
	taiwins_launcher_destroy(launcher->interface);
	wl_globals_release(&launcher->globals);

	launcher->quit = true;
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;

	if (strcmp(interface, taiwins_launcher_interface.name) == 0) {
		fprintf(stderr, "launcher registé\n");
		launcher->interface = (struct taiwins_launcher *)
			wl_registry_bind(wl_registry, name, &taiwins_launcher_interface, version);
		taiwins_launcher_add_listener(launcher->interface, &launcher_impl, launcher);
	} else
		wl_globals_announce(&launcher->globals, wl_registry, name, interface, version);
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
	struct desktop_launcher tw_launcher;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	wl_globals_init(&tw_launcher.globals, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &tw_launcher);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	init_launcher(&tw_launcher);

	//okay, now we should create the buffers
	//event loop
	while(wl_display_dispatch(display) != -1 && !tw_launcher.quit);
	release_launcher(&tw_launcher);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
