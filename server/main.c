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

static struct tw_key_press cxx[MAX_KEY_SEQ_LEN] = {
	{KEY_X+8, MODIFIER_CTRL},
	{KEY_X+8, 0},
	{0},
	{0},
	{0},
};

static struct tw_key_press cxc[MAX_KEY_SEQ_LEN] = {
	{KEY_X+8, MODIFIER_CTRL},
	{KEY_C+8, 0},
	{0},
	{0},
	{0},
};

static struct tw_btn_press cl[MAX_KEY_SEQ_LEN] = {
	{BTN_LEFT, MODIFIER_CTRL},
	{0},
	{0},
	{0},
	{0},
};


void print_key(struct weston_keyboard *keyboard, uint32_t option, void *data)
{
	fprintf(stderr, "hey, look, we just pressed %c!\n", option);
}

void print_btn(struct weston_pointer *pointer, uint32_t option, void *data)
{
	fprintf(stderr, "hey, look, we just hit a button %c\n", option);
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

	struct tw_binding_node *keybindings = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(keybindings); //init as root
	tw_binding_add_key(keybindings, cxx, print_key, 'x', NULL);
	tw_binding_add_key(keybindings, cxc, print_key, 'c', NULL);
	tw_bindings_apply_to_compositor(keybindings, compositor);


	struct tw_binding_node *btnbindings = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(btnbindings);
	btnbindings->type = TW_BINDING_btn;
	tw_binding_add_btn(btnbindings, cl, print_btn, 'l', NULL);
	tw_bindings_apply_to_compositor(btnbindings, compositor);


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


	weston_compositor_shutdown(compositor);
	weston_compositor_destroy(compositor);
	wl_display_destroy(display);
	return 0;
connect_err:
	wl_display_destroy(display);
	return -1;
}
