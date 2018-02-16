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

//this is a really bad idea
struct tw_resources {

};

//remove this two later
static struct weston_seat g_seat;
struct weston_seat *seat0;

static int
tw_log(const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

bool setup_input(struct weston_compositor *compositor)
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
	weston_seat_init_keyboard(seat0, compositor->xkb_info->keymap); //if something goes wrong
//	weston_seat_update_keymap(seat0, compositor->xkb_info->keymap);
//	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat0);
	//you can also do this
	weston_seat_init_pointer(seat0);
//	weston_seat_init_touch(seat0);
	return true;
}

void
tawins_quit(struct weston_compositor *c)
{
	if (c->xkb_info) {
		xkb_keymap_unref(c->xkb_info->keymap);
		free(c->xkb_info);
		c->xkb_info = NULL;
	}
	if (c->xkb_context) {
		xkb_context_unref(c->xkb_context);
		c->xkb_context = NULL;
	}
}

//todo: cursor, loading weston-terminal using keyboard (for this, do we need the
//the new protocol or something, ), but right now just show the cursor
int main(int argc, char *argv[])
{
	weston_log_set_handler(tw_log, tw_log);
	struct wl_display *display = wl_display_create();
	if (wl_display_add_socket(display, NULL) == -1)
		goto connect_err;

	struct weston_compositor *compositor = weston_compositor_create(display, NULL);
	//yep, we need to setup backend
	tw_setup_backend(compositor);
	setup_input(compositor);
	fprintf(stderr, "backend registred\n");

	weston_compositor_wake(compositor);
	//okay, now it is a good time to anonce the shell
	announce_shell(compositor);
	wl_display_run(display);
	weston_compositor_destroy(compositor);
	return 0;
setup_err:
	weston_compositor_destroy(compositor);
connect_err:
	wl_display_destroy(display);
	return -1;
}
