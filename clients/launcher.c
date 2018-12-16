#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-client.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include <os/exec.h>

#include "../config.h"
#include "client.h"
#include "ui.h"
#include "nk_backends.h"


struct desktop_launcher {
	struct taiwins_launcher *interface;
	struct wl_globals globals;
	struct app_surface surface;
	struct shm_pool pool;
	struct wl_buffer *decision_buffer;

	struct wl_callback *exec_cb;
	uint32_t exec_id;

	off_t cursor;
	char chars[256];
	bool quit;
	//rendering data
	//for nuklear
	struct nk_wl_backend *bkend;
	//a good hack is that this text_edit is stateless, we don't need to
	//store anything once submitted
	struct nk_text_edit text_edit;
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

static void
submit_launcher(struct app_surface *surf)
{
	struct desktop_launcher *launcher =
		container_of(surf, struct desktop_launcher, surface);
	taiwins_launcher_submit(launcher->interface, launcher->decision_buffer, launcher->exec_id);
	nk_textedit_init_fixed(&launcher->text_edit, launcher->chars, 256);
	app_surface_release(&launcher->surface);
}


/**
 * @brief a more serious launcher implementation
 */
static void
draw_launcher(struct nk_context *ctx, float width, float height,
	      struct app_surface *surf)
{
	//TODO change the state machine
	enum EDITSTATE {NORMAL, COMPLETING, SUBMITTING};
	static enum EDITSTATE edit_state = NORMAL;
	static char previous_tab[256] = {0};

	struct desktop_launcher *launcher =
		container_of(surf, struct desktop_launcher, surface);

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);

	nk_layout_row_static(ctx, height - 30, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &launcher->text_edit, nk_filter_default);
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
		nk_textedit_delete(&launcher->text_edit, launcher->text_edit.cursor - strlen(previous_tab),
				   strlen(previous_tab));
		strcpy(previous_tab, auto_complete(launcher));
		nk_textedit_text(&launcher->text_edit, previous_tab, strlen(previous_tab));
		break;
	case SUBMITTING:
		memset(previous_tab, 0, sizeof(previous_tab));
		edit_state = NORMAL;
		nk_wl_add_idle(ctx, submit_launcher);
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
	struct app_surface *surface = &launcher->surface;
	struct wl_surface *wl_surface = NULL;
	struct tw_ui *ui;
	surface->w = wl_fixed_to_int(width);
	surface->h = wl_fixed_to_int(height);
	surface->s = wl_fixed_to_int(scale);

	wl_surface = wl_compositor_create_surface(launcher->globals.compositor);
	ui = taiwins_launcher_launch(taiwins_launcher, wl_surface);

	app_surface_init(surface, wl_surface, (struct wl_proxy *)ui);
	nk_egl_impl_app_surface(surface, launcher->bkend, draw_launcher,
				400, 400, 0, 0);
	app_surface_frame(surface, false);
}

static void
exec_application(void *data, struct taiwins_launcher *launcher, uint32_t id)
{
	char *const forks[] = {"/usr/bin/weston-terminal", NULL};
	struct desktop_launcher *desktop_launcher = data;
	if (id != desktop_launcher->exec_id) {
		fprintf(stderr, "exec order not consistant, something wrong.");
	} else {
		fprintf(stderr, "creating weston terminal");
		//parsing the input and command buffer. Then do it
//		fork_exec(1, forks);
	}
	desktop_launcher->exec_id++;
}

struct taiwins_launcher_listener launcher_impl = {
	.application_configure = update_app_config,
	.start = start_launcher,
	.exec = exec_application,
};

/** constructor-destructor **/
static void
init_launcher(struct desktop_launcher *launcher)
{
	memset(launcher->chars, 0, sizeof(launcher->chars));
	launcher->quit = false;
	shm_pool_init(&launcher->pool, launcher->globals.shm,
		      TAIWINS_LAUNCHER_CONF_NUM_DECISIONS * sizeof(struct taiwins_decision_key),
		      launcher->globals.buffer_format);
	launcher->decision_buffer = shm_pool_alloc_buffer(&launcher->pool,
							  sizeof(struct taiwins_decision_key),
							  TAIWINS_LAUNCHER_CONF_NUM_DECISIONS);

	launcher->bkend = nk_egl_create_backend(launcher->globals.display, NULL);
	nk_textedit_init_fixed(&launcher->text_edit, launcher->chars, 256);
}


static void
release_launcher(struct desktop_launcher *launcher)
{
	nk_textedit_free(&launcher->text_edit);
	nk_egl_destroy_backend(launcher->bkend);
	shm_pool_release(&launcher->pool);

	taiwins_launcher_destroy(launcher->interface);
	wl_globals_release(&launcher->globals);

	launcher->quit = true;
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
