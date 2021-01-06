#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wayland-server-core.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/profiler.h>
#include <taiwins/render_context.h>
#include <taiwins/render_pipeline.h>
#include <taiwins/render_output.h>
#include <taiwins/backend_wayland.h>
#include <taiwins/engine.h>
#include <taiwins/xdg.h>

#include <wayland-util.h>

struct tw_render_pipeline *
tw_egl_render_pipeline_create_default(struct tw_render_context *ctx,
                                      struct tw_layers_manager *manager);
static void
set_dirty(void *data)
{
	struct tw_render_output *output = data;
	output->state.repaint_state = TW_REPAINT_DIRTY;
}

static void
dummy_repaint(struct tw_render_pipeline *pipeline,
              struct tw_render_output *output, int buffer_age)
{
	struct wl_display *display = output->ctx->display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	wl_event_loop_add_idle(loop, set_dirty, output);
	glClearColor(1.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void
dummy_destroy(struct tw_render_pipeline *pipeline)
{
	tw_render_pipeline_fini(pipeline);
}

struct tw_render_pipeline dummy_pipeline = {
	.impl.repaint_output = dummy_repaint,
	.impl.destroy = dummy_destroy,
};

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
	tw_profiler_open(display, "/tmp/drm-profiler.json");

	loop = wl_display_get_event_loop(display);
	struct wl_event_source *sigint =
		wl_event_loop_add_signal(loop, SIGINT,
		                         tw_term_on_signal, display);
	struct tw_backend *backend =
		tw_wl_backend_create(display, getenv("WAYLAND_DISPLAY"));
	struct tw_engine *engine =
		tw_engine_create_global(display, backend);
	struct tw_xdg *xdg =
		tw_xdg_create_global(display, NULL, engine);
	if (!backend)
		goto err;
	const struct tw_egl_options *opts =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opts);
	if (!tw_wl_backend_new_output(backend, 1000, 1000))
		goto err;
	if (!tw_wl_backend_new_output(backend, 900, 900))
		goto err;
	(void)xdg;

	wl_display_add_socket_auto(display);

	struct tw_render_pipeline *pipeline =
		tw_egl_render_pipeline_create_default(ctx,
		                                      &engine->layers_manager);
	wl_list_insert(ctx->pipelines.next, &pipeline->link);

	tw_backend_start(backend, ctx);

	wl_display_run(display);
	wl_event_source_remove(sigint);

err:
	tw_profiler_close();
	wl_display_destroy(display);
	return EXIT_FAILURE;
}
