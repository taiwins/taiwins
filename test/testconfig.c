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

static bool
dummpy_apply(struct taiwins_config *c, struct taiwins_option_listener *l)
{
	fprintf(stderr, "applying configuration\n");
	return true;
}


struct taiwins_option_listener option = {
	.type = TW_OPTION_RGB,
	.apply = dummpy_apply,
};

int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_compositor *ec = weston_compositor_create(display, NULL);
	struct taiwins_config *config = taiwins_config_create(ec, vprintf);

	taiwins_config_add_option_listener(config, "bigmac", &option);

	for (int i = 0; i < 10; i++)
		taiwins_run_config(config, argv[1]);

	taiwins_config_destroy(config);
	weston_compositor_shutdown(ec);
	weston_compositor_destroy(ec);
	wl_display_terminate(display);
	wl_display_destroy(display);
	return 0;
}
