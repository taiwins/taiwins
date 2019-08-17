#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <wayland-server.h>
#include <compositor.h>
#include "../server/config.h"


int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_compositor *ec = weston_compositor_create(display, NULL);
	struct taiwins_config *config = taiwins_config_create(ec, vprintf);
	struct tw_bindings *bindings = tw_bindings_create(ec);

	taiwins_run_config(config, bindings, argv[1]);

	taiwins_config_destroy(config);
	tw_bindings_destroy(bindings);
	weston_compositor_shutdown(ec);
	weston_compositor_destroy(ec);
	wl_display_destroy(display);
	return 0;
}
