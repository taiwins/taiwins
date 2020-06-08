#include "twclient/ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <twclient/client.h>
#include <twclient/shmpool.h>


static void
dummy_draw(struct tw_appsurf *surf, struct wl_buffer *buffer,
           struct tw_bbox *geo)
{
	void *buffer_data = tw_shm_pool_buffer_access(buffer);
	memset(buffer_data, 255,
	       surf->allocation.w * surf->allocation.h * 4);
}

static
void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	struct tw_globals *globals = data;
	tw_globals_announce(globals, wl_registry, name, interface, version);
}

static
void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{
	fprintf(stderr, "global %d removed", name);
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};

int main(int argc, char *argv[])
{
	struct tw_appsurf app;
	struct tw_globals tw_globals;
	struct wl_display *wl_display = wl_display_connect(NULL);
	tw_globals_init(&tw_globals, wl_display);
	if (!wl_display) {
		fprintf(stderr, "no display available\n");
		return -1;
	}

	struct wl_registry *registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(registry, &registry_listener, &tw_globals);

	wl_display_dispatch(wl_display);
	wl_display_roundtrip(wl_display);

	struct wl_surface *surface =
		wl_compositor_create_surface(tw_globals.compositor);
	tw_appsurf_init(&app, surface, &tw_globals, TW_APPSURF_WIDGET, 0);
	shm_buffer_impl_app_surface(&app, dummy_draw,
                            tw_make_bbox_origin(400, 200, 1));
	wl_display_flush(wl_display);
	tw_globals_dispatch_event_queue(&tw_globals);
	tw_globals_release(&tw_globals);
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);

	return 0;
}
