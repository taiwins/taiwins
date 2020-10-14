#include "taiwins/backend/backend.h"
#include "taiwins/render_context.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wayland-server-core.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <wayland-util.h>

#include <taiwins/backend/wayland.h>

static int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %s\n", strsignal(sig_num));
	wl_display_terminate(display);
	return 1;
}


int main(int argc, char *argv[])
{
	struct wl_display *display;
	struct wl_event_loop *loop;

	tw_logger_use_file(stderr);
	display = wl_display_create();
	if (!display)
		return EXIT_FAILURE;
	loop = wl_display_get_event_loop(display);
	struct wl_event_source *sigint =
		wl_event_loop_add_signal(loop, SIGINT,
		                         tw_term_on_signal, display);

	struct tw_backend *backend =
		tw_wayland_backend_create(display, getenv("WAYLAND_DISPLAY"));
	if (!backend)
		goto err;
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opts);
	tw_backend_start(backend, ctx);

	wl_display_run(display);
	wl_event_source_remove(sigint);

err:
	wl_display_destroy(display);
	return EXIT_FAILURE;
}
