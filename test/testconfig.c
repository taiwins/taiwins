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
#include <strops.h>
#include <sequential.h>
#include <theme.h>

#include "../server/taiwins.h"
#include "../server/config.h"
#include "../server/config/lua_helper.h"

static bool
dummy_apply(struct tw_config *c, struct tw_option_listener *l)
{
	fprintf(stderr, "applying configuration\n");
	return true;
}


struct tw_option_listener option = {
	.type = TW_OPTION_RGB,
	.apply = dummy_apply,
};

extern int tw_theme_read(lua_State *L);

struct tw_config_component_listener lua_component = {
	.link = {
		&lua_component.link, &lua_component.link,},
};

int
main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_create();
	struct weston_log_context *context = weston_log_ctx_compositor_create();
	struct weston_compositor *ec = weston_compositor_create(display, context, NULL);
	struct tw_config *config = tw_config_create(ec, vprintf);

	tw_config_add_option_listener(config, "bigmac", &option);
	tw_config_add_component(config, &lua_component);

	//TODO we can actually get the shell/desktop/theme config here

	tw_config_run(config, argv[1]);

	tw_config_destroy(config);
	weston_compositor_tear_down(ec);
	weston_log_ctx_compositor_destroy(ec);
	weston_compositor_destroy(ec);
	wl_display_terminate(display);
	wl_display_destroy(display);
	return 0;
}
