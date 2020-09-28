#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <taiwins/objects/logger.h>

#include "backend/x11.h"
#include "egl.h"
#include "render_context.h"

int main(int argc, char *argv[])
{
	struct wl_display *display;
	struct tw_backend backend = {0};

	tw_logger_use_file(stderr);
	display = wl_display_create();
	if (!display)
		return EXIT_FAILURE;

	backend.impl = tw_x11_backend_create(display, getenv("DISPLAY"));
	if (!backend.impl)
		goto err;
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(&backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opts);
	tw_x11_backend_add_output(&backend, 1000, 1000);
	backend.impl->start(&backend, ctx);

	wl_display_run(display);

	tw_render_context_destroy(ctx);
	wl_display_destroy(display);

	return 0;
err:
	wl_display_destroy(display);
	return EXIT_FAILURE;
}
