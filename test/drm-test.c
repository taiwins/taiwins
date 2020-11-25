#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <taiwins/backend_drm.h>
#include <taiwins/render_output.h>
#include <taiwins/objects/egl.h>
#include <taiwins/render_context.h>
#include <taiwins/render_pipeline.h>
#include <taiwins/objects/logger.h>
#include <wayland-server-core.h>

static inline void
wait_for_debug()
{
#define DEBUG_FILE "/tmp/waitfordebug"
	int fd = -1;

	while ((fd = open(DEBUG_FILE, O_RDONLY)) < 0)
		usleep(100000);

	unlink(DEBUG_FILE);
}


static int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %s\n", strsignal(sig_num));
	wl_display_terminate(display);
	return 1;
}

static void
set_dirty(void *data)
{
	struct tw_render_output *output = data;
	output->state.repaint_state = TW_REPAINT_DIRTY;
}

static void
dummy_destroy(struct tw_render_pipeline *pipeline)
{
	tw_render_pipeline_fini(pipeline);
}

#define MAX(a, b) (a > b) ? a : b
#define MIN(a, b) (a < b) ? a : b

static inline float fract(float x) { return x - (int)x; }

static inline float mix(float a, float b, float t) { return a + (b - a) * t; }

static inline float absf(float x)
{
	return (x >= 0.0) ? x : -x;
}

static inline float constrain(float x, float l, float h)
{
	return MIN(MAX(x, l), h);
}

static inline void hsv2rgb(float h, float s, float b, float* rgb) {
  rgb[0] = b * mix(1.0, constrain(absf(fract(h + 1.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
  rgb[1] = b * mix(1.0, constrain(absf(fract(h + 0.6666666) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
  rgb[2] = b * mix(1.0, constrain(absf(fract(h + 0.3333333) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
}

static void
dummy_repaint(struct tw_render_pipeline *pipeline,
              struct tw_render_output *output, int buffer_age)
{
	static int h = 0;
	float rgb[3];
	struct wl_display *display = output->ctx->display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	wl_event_loop_add_idle(loop, set_dirty, output);
	hsv2rgb(h / 255.0, 1.0, 1.0, rgb);
	glClearColor(rgb[0], rgb[1], rgb[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	h = (h + 1) % 256;
}

static struct tw_render_pipeline dummy_pipeline = {
	.impl.repaint_output = dummy_repaint,
	.impl.destroy = dummy_destroy,
};


void
test_switch_vt(struct tw_backend *backend)
{
	struct tw_login *login = tw_drm_backend_get_login(backend);

	tw_login_switch_vt(login, 3);
}

int main(int argc, char *argv[])
{
	tw_logger_use_file(stderr);

	wait_for_debug();

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	struct tw_backend *backend = tw_drm_backend_create(display);
	const struct tw_egl_options *opt =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opt);

	struct wl_event_source *sigint =
		wl_event_loop_add_signal(loop, SIGINT,
		                         tw_term_on_signal, display);
	tw_render_pipeline_init(&dummy_pipeline, "dummy_pipeline", ctx);
	wl_list_insert(ctx->pipelines.next, &dummy_pipeline.link);

	tw_backend_start(backend, ctx);

	wl_display_run(display);

	wl_event_source_remove(sigint);
	wl_display_destroy(display);
	return 0;
}
