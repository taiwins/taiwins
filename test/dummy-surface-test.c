#include "twclient/ui.h"
#include "twclient/ui_event.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <twclient/client.h>
#include <twclient/shmpool.h>

static struct wl_shell *s_wl_shell = NULL;

static void
dummy_draw(struct tw_appsurf *surf, struct wl_buffer *buffer,
           struct tw_bbox *geo)
{
	struct tw_app_event event = {
		.type = TW_RESIZE,
		.resize = {
			.nw = surf->allocation.w,
			.nh = surf->allocation.h,
			.ns = (surf->allocation.s == 1) ? 2 : 1,
		},
	};
	static unsigned color = 0;
	void *buffer_data = tw_shm_pool_buffer_access(buffer);

	color = (color + 1) % 100;
	memset(buffer_data, color+156,
	       surf->allocation.w * surf->allocation.h * 4 *
	       surf->allocation.s * surf->allocation.s);
	*geo = tw_make_bbox(0, 0, surf->allocation.w, surf->allocation.h,
	                    surf->allocation.s);
	tw_appsurf_request_frame_event(surf, &event);
}

static
void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	struct tw_globals *globals = data;
	if (strcmp(interface, wl_shell_interface.name) == 0) {
		s_wl_shell = wl_registry_bind(wl_registry, name,
		                             &wl_shell_interface, version);
		fprintf(stdout, "wl_shell %d announced\n", name);
	}

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

static void
handle_shell_ping(void *data, struct wl_shell_surface *wl_shell_surface,
                  uint32_t serial)
{
	wl_shell_surface_pong(wl_shell_surface, serial);
}

static void
handle_shell_configure(void *data, struct wl_shell_surface *shell_surface,
                       uint32_t edges, int32_t width, int32_t height)
{
	struct tw_appsurf *app = data;
	tw_appsurf_resize(app, width, height, app->allocation.s);
}

static void
shell_handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}


static struct wl_shell_surface_listener shell_impl = {
	.ping = handle_shell_ping,
	.configure = handle_shell_configure,
	.popup_done = shell_handle_popup_done,
};


int main(int argc, char *argv[])
{
	struct tw_appsurf app;
	struct tw_globals tw_globals;
	struct wl_shell_surface *shell_surface;
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

	if (s_wl_shell) {
		shell_surface = wl_shell_get_shell_surface(s_wl_shell, surface);
		wl_shell_surface_set_toplevel(shell_surface);
		wl_shell_surface_add_listener(shell_surface, &shell_impl, &app);
	}

	tw_appsurf_frame(&app, false);
	wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_180);

	tw_globals_dispatch_event_queue(&tw_globals);
	tw_globals_release(&tw_globals);
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);

	return 0;
}
