/**
 * @file test-xkbcommon.c
 *
 * common pratice of using xkbcommon as a wayland client
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>


struct seat {
	struct wl_seat *s;
	uint32_t id;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_touch *touch;
	const char *name;
	//keyboard informations
	struct xkb_context *kctxt;
	struct xkb_keymap *kmap;
	struct xkb_state  *kstate;
} seat0;

//struct wl_seat *seat0;

//keyboard listener, you will definitly announce the
void handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		   uint32_t format,
		   int32_t fd,
		   uint32_t size)
{
	struct seat *seat0 = (struct seat *)data;
	//now it is the time to creat a context
	seat0->kctxt = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	seat0->kmap = xkb_keymap_new_from_buffer(seat0->kctxt, addr, size, format, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(addr, size);
	seat0->kstate = xkb_state_new(seat0->kmap);
}

void handle_key(void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	fprintf(stderr, "got a key %d\n", key);
	//now it is time to decode
}

void handle_modifiers(void *data,
		      struct wl_keyboard *wl_keyboard,
		      uint32_t serial,
		      uint32_t mods_depressed, //which key
		      uint32_t mods_latched,
		      uint32_t mods_locked,
		      uint32_t group)
{
	//I guess this serial number is different for each event
	struct seat *seat0 = (struct seat *)data;
	//wayland uses layout group. you need to know what xkb_matched_layout is
	xkb_state_update_mask(seat0->kstate, mods_depressed, mods_latched, mods_locked, 0, 0, 0);
}


struct wl_keyboard_listener keyboard_listener = {
	.key = handle_key,
	.modifiers = handle_modifiers,
};



void seat_capabilities(void *data,
		       struct wl_seat *wl_seat,
		       uint32_t capabilities)
{
	struct seat *seat0 = (struct seat *)data;
	if (capabilities == WL_SEAT_CAPABILITY_KEYBOARD) {
		seat0->keyboard = wl_seat_get_keyboard(wl_seat);
		fprintf(stderr, "got a keyboard\n");
		wl_keyboard_add_listener(seat0->keyboard, &keyboard_listener, seat0);
	} else if (capabilities == WL_SEAT_CAPABILITY_POINTER) {
		seat0->pointer = wl_seat_get_pointer(wl_seat);
		fprintf(stderr, "got a mouse\n");
	} else if (capabilities == WL_SEAT_CAPABILITY_TOUCH) {
		seat0->touch = wl_seat_get_touch(wl_seat);
		fprintf(stderr, "got a touchpad\n");
	}
}

void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	struct seat *seat0 = (struct seat *)data;
	fprintf(stderr, "we have this seat with a name called %s\n", name);
	seat0->name = name;
}


struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};


void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	(void)data;
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat0.id = name;
		seat0.s = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(seat0.s, &seat_listener, &seat0);
	}

}

void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{
	if (name == seat0.id) {
		fprintf(stderr, "wl_seat removed");
	}

}



struct wl_registry_listener registry_listener = {
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

	wl_display_disconnect(display);
	return 0;
}
