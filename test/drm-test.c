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

int main(int argc, char *argv[])
{
	tw_logger_use_file(stderr);

	/* wait_for_debug(); */

	struct wl_display *display = wl_display_create();
	struct tw_backend *backend = tw_drm_backend_create(display);
	struct tw_login *login = tw_drm_backend_get_login(backend);

	int gpu = tw_login_find_primary_gpu(login);
	if (gpu >= 0) {
		close(gpu);
	} else {
		assert(0);
	}
	tw_login_switch_vt(login, 3);

	wl_display_destroy(display);
	return 0;
}
