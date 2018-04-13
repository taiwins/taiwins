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

struct desktop_launcher {
	struct taiwins_shell *shell;
	struct wl_globals globals;
	struct app_surface launcher_surface;
	struct wl_buffer *decision_buffer;

	bool quit;
};


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;

	if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		launcher->shell = (struct taiwins_shell *)
			wl_registry_bind(wl_registry, name, &taiwins_shell_interface, version);
//		taiwins_shell_add_listener(twshell->shell, &taiwins_listener, twshell);
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


static void
desktop_launcher_init(struct desktop_launcher *launcher, struct wl_display *wl_display)
{
	wl_globals_init(&launcher->globals, wl_display);
	launcher->shell = NULL;
	launcher->quit = false;
	wl_display_set_user_data(wl_display, launcher);
}


static void
desktop_launcher_release(struct desktop_launcher *launcher)
{
	taiwins_shell_destroy(launcher->shell);
	wl_globals_release(&launcher->globals);
	launcher->quit = true;
}


int
main(int argc, char *argv[])
{
	struct desktop_launcher onelauncher;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	desktop_launcher_init(&onelauncher, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &onelauncher);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	//event loop
	while(wl_display_dispatch(display) != -1 && !onelauncher.quit);
	desktop_launcher_release(&onelauncher);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
