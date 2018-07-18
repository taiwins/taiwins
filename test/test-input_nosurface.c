/**
 * @file test-xkbcommon.c
 *
 * common pratice of using xkbcommon as a wayland client
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <search.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>
#include <linux/input.h>

#include <cairo.h>

#include <os/buffer.h>


static struct wl_seat *gseat;
static struct wl_keyboard *gkeyboard;
static struct wl_touch *gtouch;
static struct wl_pointer *gpointer;
static struct wl_shell *gshell;
static struct wl_compositor *gcompositor;

//keyboard listener, you will definitly announce the
static void handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		   uint32_t format,
		   int32_t fd,
		   uint32_t size)
{
	fprintf(stderr, "new keymap available\n");
}
static
void handle_key(void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	if (key == KEY_ESC)
		return;
	if (!state) //lets hope the server side has this as well
		return;
	struct seat *seat0 = (struct seat *)data;
	fprintf(stderr, "key %d %s\n", key, state ? "pressed" : "released");
}

static
void handle_keyboard_enter(void *data,
			   struct wl_keyboard *wl_keyboard,
			   uint32_t serial,
			   struct wl_surface *surface,
			   struct wl_array *keys)
{
	fprintf(stderr, "keyboard got focus\n");
}
static
void handle_keyboard_leave(void *data,
		    struct wl_keyboard *wl_keyboard,
		    uint32_t serial,
		    struct wl_surface *surface)
{
	fprintf(stderr, "keyboard lost focus\n");
}
static
void handle_modifiers(void *data,
		      struct wl_keyboard *wl_keyboard,
		      uint32_t serial,
		      uint32_t mods_depressed, //which key
		      uint32_t mods_latched,
		      uint32_t mods_locked,
		      uint32_t group)
{
	fprintf(stderr, "We pressed a modifier\n");
	//I guess this serial number is different for each event
	struct seat *seat0 = (struct seat *)data;

}



//you must have this
static
void handle_repeat_info(void *data,
			    struct wl_keyboard *wl_keyboard,
			    int32_t rate,
			    int32_t delay)
{

}



static struct wl_keyboard_listener keyboard_impl = {
	.key = handle_key,
	.modifiers = handle_modifiers,
	.enter = handle_keyboard_enter,
	.leave = handle_keyboard_leave,
	.keymap = handle_keymap,
	.repeat_info = handle_repeat_info,
};




static
void seat_capabilities(void *data,
		       struct wl_seat *wl_seat,
		       uint32_t capabilities)
{
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		gkeyboard = wl_seat_get_keyboard(wl_seat);
		fprintf(stderr, "this seat %s has a keyboard\n", wl_seat);
		wl_keyboard_add_listener(gkeyboard, &keyboard_impl, gseat);
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		gpointer = wl_seat_get_pointer(wl_seat);
		fprintf(stderr, "got a mouse\n");
//		//okay, load the cursor stuff
//		struct wl_cursor_theme *theme = wl_cursor_theme_load("Vanilla-DMZ", 32, shm);
//		seat0->cursor_theme = theme;
// //		tw_cursor_theme_print_cursor_names(theme);
//		struct wl_cursor *plus = wl_cursor_theme_get_cursor(theme, "plus");
//		struct wl_buffer *first = wl_cursor_image_get_buffer(plus->images[0]);
//		struct wl_surface *surface = wl_compositor_create_surface(gcompositor);
//		wl_surface_set_user_data(surface, first);
//		wl_pointer_set_user_data(seat0->pointer, surface);
//		wl_pointer_add_listener(seat0->pointer, &pointer_listener, surface);
		//showing the cursor image

//		tw_cursor_theme_destroy(theme);
	}
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		gtouch = wl_seat_get_touch(wl_seat);
		fprintf(stderr, "got a touchpad\n");
	}
}


static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	fprintf(stderr, "we have this seat with a name called %s\n", name);
}


static
struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};


static void
announce_globals(void *data,
		 struct wl_registry *wl_registry,
		 uint32_t name,
		 const char *interface,
		 uint32_t version)
{
	(void)data;
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		fprintf(stderr, "got a seat\n, preparing seat listener\n");
		gseat = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(gseat, &seat_listener, &gseat);
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		fprintf(stderr, "announcing the shell\n");
		gshell = wl_registry_bind(wl_registry, name, &wl_shell_interface, version);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		fprintf(stderr, "announcing the compositor\n");
		gcompositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	}


}


static
void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{

}


static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


int main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		return -1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	while(wl_display_dispatch(display) != -1);


	return 0;
}
