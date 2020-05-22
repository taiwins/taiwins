#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>


// For this, we would probably start come up with a backend code

struct tw_backend {
	bool defer_output_creation;

	struct wl_signal new_heads;

	struct wl_list heads; /**< all the physicall outputs */
};

/**
 * @brief flush all the changes of the output.
 */
void
tw_backend_flush(struct tw_backend *backend);

struct tw_compositor {
	struct wl_display *display;
	struct wl_event_loop *loop; /**< main event loop */
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;

	struct wl_list output;
};

int
main(int argc, char *argv[])
{


	return 0;
}
