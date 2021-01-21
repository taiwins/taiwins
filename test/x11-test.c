#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wayland-server-core.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <wayland-util.h>

#include <taiwins/backend_x11.h>
#include <taiwins/render_context.h>
#include <taiwins/render_pipeline.h>
#include <taiwins/engine.h>
#include "test_desktop.h"

struct data {
	struct wl_display *display;
	struct tw_engine *engine;
	struct tw_test_desktop *desktop;

	struct {
		struct wl_listener xserver_ready;
	} listeners;
};

struct tw_render_pipeline *
tw_egl_render_pipeline_create_default(struct tw_render_context *ctx,
                                      struct tw_layers_manager *manager);

static int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %s\n", strsignal(sig_num));
	wl_display_terminate(display);
	return 1;
}

static int
tw_term_on_timeout(void *data)
{
	struct wl_display *display = data;

	tw_logl("Time out");
	wl_display_terminate(display);
	return 0;
}

int main(int argc, char *argv[])
{
	struct wl_display *display;
	struct wl_event_loop *loop;
	struct tw_backend *backend;
	struct tw_test_desktop desktop;

	tw_logger_use_file(stderr);
	display = wl_display_create();
	if (!display)
		return EXIT_FAILURE;
	loop = wl_display_get_event_loop(display);

	wl_display_add_socket_auto(display);

	struct wl_event_source *sigint =
		wl_event_loop_add_signal(loop, SIGINT,
		                         tw_term_on_signal, display);
	struct wl_event_source *timeout =
		wl_event_loop_add_timer(loop, tw_term_on_timeout, display);

	if (!sigint || !timeout) {
		tw_logl_level(TW_LOG_ERRO, "failed to add signal");
		return EXIT_FAILURE;
	}

	backend = tw_x11_backend_create(display, getenv("DISPLAY"));
	if (!backend)
		goto err;
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opts);
	tw_x11_backend_add_output(backend, 1000, 1000);

	struct tw_engine *engine =
		tw_engine_create_global(display, backend);
	if (!engine)
		goto err;
	struct tw_render_pipeline *pipeline =
		tw_egl_render_pipeline_create_default(ctx,
		                                      &engine->layers_manager);
	wl_list_insert(ctx->pipelines.prev, &pipeline->link);
        tw_test_desktop_init(&desktop, engine);

	tw_backend_start(backend, ctx);

	if (wl_event_source_timer_update(timeout, 3000)) {
		tw_logl_level(TW_LOG_ERRO, "timer update failed");
		return EXIT_FAILURE;
	}

	wl_display_run(display);
	wl_event_source_remove(sigint);
	wl_event_source_remove(timeout);

	tw_test_desktop_fini(&desktop);
	tw_render_context_destroy(ctx);
	wl_display_destroy(display);

	return 0;
err:
	wl_display_destroy(display);
	return EXIT_FAILURE;
}
