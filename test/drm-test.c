#include "taiwins/backend.h"
#include "taiwins/objects/egl.h"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <taiwins/backend_drm.h>
#include <taiwins/render_context.h>
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


int
tw_login_find_primary_gpu(struct tw_login *login);

void
test_switch_vt(struct tw_backend *backend)
{
	struct tw_login *login = tw_drm_backend_get_login(backend);

	int gpu = tw_login_find_primary_gpu(login);
	if (gpu >= 0) {
		close(gpu);
	} else {
		assert(0);
	}
	tw_login_switch_vt(login, 3);
}

int main(int argc, char *argv[])
{
	tw_logger_use_file(stderr);

	wait_for_debug();

	struct wl_display *display = wl_display_create();
	struct tw_backend *backend = tw_drm_backend_create(display);
	const struct tw_egl_options *opt =
		tw_backend_get_egl_params(backend);
	struct tw_render_context *ctx =
		tw_render_context_create_egl(display, opt);
	tw_backend_start(backend, ctx);

	wl_display_destroy(display);
	return 0;
}
