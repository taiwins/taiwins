#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/backend/headless.h>
#include <taiwins/render_context.h>

int main(int argc, char *argv[])
{
	//TODO; we will want to test wayland, X11, and headless, for now, lets
	//test headless
	struct wl_display *display;
	struct tw_backend *backend;

	tw_logger_use_file(stderr);
	display = wl_display_create();
	if (!display)
		return EXIT_FAILURE;

	backend = tw_headless_backend_create(display);
	if (!backend)
		goto err;
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opts);
	tw_render_context_destroy(ctx);

	wl_display_destroy(display);
	return 0;
err:
	wl_display_destroy(display);
	return EXIT_FAILURE;
}
