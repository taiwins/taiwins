#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <wayland-server.h>
#include <compositor.h>
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <zalloc.h>
#include <windowed-output-api.h>
#include <libweston-desktop.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "backend.h"
#include "desktop.h"
#include "taiwins.h"
#include "config.h"
#include "bindings.h"

//remove this two later

struct tw_compositor {
	struct weston_compositor *ec;

	struct taiwins_apply_bindings_listener add_binding;
	struct taiwins_config_component_listener config_component;
};


static FILE *logfile = NULL;

static int
tw_log(const char *format, va_list args)
{
	return vfprintf(logfile, format, args);
}

/*
static bool
setup_input(struct weston_compositor *compositor)
{
	if (!compositor->xkb_names.layout) {
		compositor->xkb_names = (struct xkb_rule_names) {
			.rules = NULL,
			.model = "pc105",
			.layout = "us",
			.options = "ctrl:swap_lalt_lctl"
		};
		compositor->xkb_info = (struct weston_xkb_info *)zalloc(sizeof(struct weston_xkb_info));
		compositor->xkb_info->keymap = xkb_keymap_new_from_names(compositor->xkb_context,
									 &compositor->xkb_names,
									 XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	//later on we can have
	//xkb_keymap_new_from_string(context, string, format, flag) or from_file
	//change every shit below!
	//TODO this is temporary code, we need to change this with libinput
	if (wl_list_empty(&compositor->seat_list)) {
		//this will append the seat to the list of compositor and call
		//the seat_create_signal
		weston_seat_init(&g_seat, compositor, "seat0");
	}
	seat0 = wl_container_of(compositor->seat_list.next, seat0, link);
	//we already updated keyboard state here
//	weston_seat_init_keyboard(seat0, compositor->xkb_info->keymap); //if something goes wrong
//	weston_seat_update_keymap(seat0, compositor->xkb_info->keymap);
//	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat0);
	//you can also do this
//	weston_seat_init_pointer(seat0);
//	weston_seat_init_touch(seat0);
	return true;
}
*/

static void
taiwins_quit(struct weston_keyboard *keyboard,
	     const struct timespec *time,
	     uint32_t key, uint32_t option,
	     void *data)
{
	struct weston_compositor *compositor = data;
	fprintf(stderr, "quitting taiwins\n");
	struct wl_display *wl_display =
		compositor->wl_display;
	wl_display_terminate(wl_display);
	exit(1);
}

static bool
tw_compositor_add_bindings(struct tw_bindings *bindings, struct taiwins_config *c,
			struct taiwins_apply_bindings_listener *listener)
{
	struct tw_compositor *tc = container_of(listener, struct tw_compositor, add_binding);
	const struct tw_key_press *quit_press =
		taiwins_config_get_builtin_binding(c, TW_QUIT_BINDING)->keypress;
	tw_bindings_add_key(bindings, quit_press, taiwins_quit, 0, tc->ec);
	return true;
}


static void
tw_compositor_init(struct tw_compositor *tc, struct weston_compositor *ec,
		   struct taiwins_config *config)
{
	tc->ec = ec;
	wl_list_init(&tc->add_binding.link);
	tc->add_binding.apply = tw_compositor_add_bindings;
	taiwins_config_add_apply_bindings(config, &tc->add_binding);
}

int main(int argc, char *argv[], char *envp[])
{
	int error = 0;
	struct tw_compositor tc;
	const char *shellpath = (argc > 1) ? argv[1] : NULL;
	const char *launcherpath = (argc > 2) ? argv[2] : NULL;
	struct wl_display *display = wl_display_create();
	char config_file[100];

	logfile = fopen("/tmp/taiwins_log", "w");
	weston_log_set_handler(tw_log, tw_log);
	//quit if we already have a wayland server
	if (wl_display_add_socket(display, NULL) == -1)
		goto connect_err;

	struct weston_compositor *compositor = weston_compositor_create(display, tw_get_backend());
	weston_log_set_handler(tw_log, tw_log);
	//apply the config now
	char *xdg_dir = getenv("XDG_CONFIG_HOME");
	if (!xdg_dir)
		xdg_dir = getenv("HOME");
	strcpy(config_file, xdg_dir);
	strcat(config_file, "/config.lua");

	tw_setup_backend(compositor);
	//it seems that we don't need to setup the input, maybe in other cases
	fprintf(stderr, "backend registred\n");
	weston_compositor_wake(compositor);
	//good moment to add the extensions
	struct taiwins_config *config = taiwins_config_create(compositor, tw_log);
	tw_compositor_init(&tc, compositor, config);
	struct shell *sh = announce_shell(compositor, shellpath, config);
	struct console *con = announce_console(compositor, sh, launcherpath, config);
	struct desktop *desktop = announce_desktop(compositor, sh, config);
	(void)con;

	error = !taiwins_run_config(config, config_file);
	if (error) {
		goto out;
	}

	compositor->kb_repeat_delay = 400;
	compositor->kb_repeat_rate = 40;

	wl_display_run(display);
out:
	
	taiwins_config_destroy(config);
//	wl_display_terminate(display);
	//now you destroy the desktops

	//TODO weston has three compositor destroy methods:
	// - weston_compositor_exit, it is
	// - weston_compositor_tear_down, I think this is the exit function
	// - weston_compositor_shutdown: remove all the bindings, output, renderer,
	// - weston_compositor_destroy, this call finally free the compositor
	/* end_desktop(desktop); */
	taiwins_config_destroy(config);

	weston_compositor_shutdown(compositor);
	weston_compositor_destroy(compositor);
	wl_display_destroy(display);
	return 0;
connect_err:
	wl_display_destroy(display);
	return -1;
}
