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
#include "shell.h"
#include "desktop.h"
#include "taiwins.h"
#include "input.h"


//remove this two later

static int
tw_log(const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
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
	     uint32_t key,
	     void *data)
{
	fprintf(stderr, "quitting taiwins\n");
	struct wl_display *wl_display = data;
	wl_display_terminate(wl_display);
	/* if (c->xkb_info) { */
	/*	xkb_keymap_unref(c->xkb_info->keymap); */
	/*	free(c->xkb_info); */
	/*	c->xkb_info = NULL; */
	/* } */
	/* if (c->xkb_context) { */
	/*	xkb_context_unref(c->xkb_context); */
	/*	c->xkb_context = NULL; */
	/* } */

}


int main(int argc, char *argv[])
{
	const char *shellpath = (argc > 1) ? argv[1] : NULL;
	const char *launcherpath = (argc > 2) ? argv[2] : NULL;
	struct wl_display *display = wl_display_create();

	weston_log_set_handler(tw_log, tw_log);
	//quit if we already have a wayland server
	if (wl_display_add_socket(display, NULL) == -1)
		goto connect_err;

	struct weston_compositor *compositor = weston_compositor_create(display, tw_get_backend());
	weston_compositor_add_key_binding(compositor, KEY_F12, 0, taiwins_quit, display);

	tw_setup_backend(compositor);
	//it seems that we don't need to setup the input, maybe in other cases
	fprintf(stderr, "backend registred\n");
	weston_compositor_wake(compositor);
	//good moment to add the extensions
	struct shell *sh = announce_shell(compositor, shellpath);
	struct console *con = announce_console(compositor, sh, launcherpath);
	struct desktop *desktop = announce_desktop(compositor);
	(void)(con);

	//deal with bindings
	struct tw_binding_node *keybindings = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(keybindings); //init as root

	struct tw_binding_node *btnbindings = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(btnbindings);
	btnbindings->type = TW_BINDING_btn;

	struct tw_binding_node *axis_bindings = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(axis_bindings);
	axis_bindings->type = TW_BINDING_axis;

	add_shell_bindings(sh, keybindings, btnbindings, axis_bindings, NULL);
	desktop_add_bindings(desktop, keybindings, btnbindings, axis_bindings, NULL);
	console_add_bindings(con, keybindings, btnbindings, axis_bindings, NULL);

	tw_bindings_apply_to_compositor(keybindings, compositor);
	tw_bindings_apply_to_compositor(btnbindings, compositor);
	tw_bindings_apply_to_compositor(axis_bindings, compositor);
	compositor->kb_repeat_delay = 400;
	compositor->kb_repeat_rate = 40;

	wl_display_run(display);

//	wl_display_terminate(display);
	//now you destroy the desktops

	//TODO weston has three compositor destroy methods:
	// - weston_compositor_exit
	// - weston_compositor_shutdown: remove all the bindings, output, renderer,
	// - weston_compositor_destroy, this call finally free the compositor
	end_desktop(desktop);
	tw_binding_destroy_nodes(keybindings);
	tw_binding_destroy_nodes(btnbindings);
	tw_binding_destroy_nodes(axis_bindings);

	/* weston_compositor_shutdown(compositor); */
	weston_compositor_destroy(compositor);
	wl_display_destroy(display);
	return 0;
connect_err:
	wl_display_destroy(display);
	return -1;
}
