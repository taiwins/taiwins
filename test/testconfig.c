#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <wayland-server.h>
#include <wayland-util.h>
#include <ctypes/strops.h>
#include <ctypes/sequential.h>
#include <twclient/theme.h>

#include "../server/taiwins.h"
#include "../server/compositor.h"
#include "../server/config/lua_helper.h"

int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_log_context *context = weston_log_ctx_compositor_create();
	struct weston_compositor *ec = weston_compositor_create(display, context, NULL);
	struct tw_config *config = tw_config_create(ec, vprintf);


	if (!tw_run_config(config))
		tw_run_default_config(config);

	tw_config_destroy(config);
	weston_compositor_tear_down(ec);
	weston_log_ctx_compositor_destroy(ec);
	weston_compositor_destroy(ec);
	wl_display_terminate(display);
	wl_display_destroy(display);
	return 0;
}
