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
	struct taiwins_launcher *interface;
	struct wl_globals globals;
	struct app_surface launcher_surface;
	struct wl_buffer *decision_buffer;

	char chars[256];
	bool quit;
};


struct taiwins_launcher_listener launcher_impl = {
	.application_configure = NULL,
};


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;

	if (strcmp(interface, taiwins_launcher_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		launcher->interface = (struct taiwins_launcher *)
			wl_registry_bind(wl_registry, name, &taiwins_launcher_interface, version);
//		taiwins_launcher_add_listener(launcher->interface, NULL, launcher);
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
	launcher->interface = NULL;
	launcher->quit = false;
	wl_display_set_user_data(wl_display, launcher);
}


static void
desktop_launcher_release(struct desktop_launcher *launcher)
{
	taiwins_launcher_destroy(launcher->interface);
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


//draw the text here
static void
_draw(struct desktop_launcher *launcher)
{
	//create the surface and destroy it in the end
	//create cairo_surface on the fly, as we uses the double buffer
	struct app_surface *surf = &launcher->launcher_surface;
	//we should have free
	if (surf->committed[1])
		return;
	void *data = shm_pool_buffer_access(surf->wl_buffer[1]);
	cairo_format_t pixel_format = translate_wl_shm_format(launcher->globals.buffer_format);
	//this is a disaster
	cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(data, pixel_format, surf->w, surf->h,
									  cairo_format_stride_for_width(pixel_format, surf->w));
}
