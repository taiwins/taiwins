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
#include <cairo.h>

#include <buffer.h>
#include "input_data.h"

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
static void handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		   uint32_t format,
		   int32_t fd,
		   uint32_t size)
{
	struct seat *seat0 = (struct seat *)data;
	//now it is the time to creat a context
	seat0->kctxt = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
//	printf("%s\n", addr);
	seat0->kmap = xkb_keymap_new_from_string(seat0->kctxt, addr, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(addr, size);
	seat0->kstate = xkb_state_new(seat0->kmap);
}
static
void handle_key(void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	if (!state) //lets hope the server side has this as well
		return;
	struct seat *seat0 = (struct seat *)data;
	run_keybinding_wayland(seat0->kstate, time, key, data);

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
	//wayland uses layout group. you need to know what xkb_matched_layout is
	xkb_state_update_mask(seat0->kstate, mods_depressed, mods_latched, mods_locked, 0, 0, group);
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

//you must have this
static
void handle_repeat_info(void *data,
			    struct wl_keyboard *wl_keyboard,
			    int32_t rate,
			    int32_t delay)
{

}



static
struct wl_keyboard_listener keyboard_listener = {
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
	void *keytable = NULL;
	struct seat *seat0 = (struct seat *)data;
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		seat0->keyboard = wl_seat_get_keyboard(wl_seat);
		fprintf(stderr, "got a keyboard\n");
		wl_keyboard_add_listener(seat0->keyboard, &keyboard_listener, seat0);
		//now add those damn short cuts
		update_tw_keymap_tree(&kp_q,  func_quit );
		update_tw_keymap_tree(&kp_of, func_of);
		update_tw_keymap_tree(&kp_cf, func_cf);
		update_tw_keymap_tree(&kp_lb, func_lb);
		update_tw_keymap_tree(&kp_bl, func_bl);
		update_tw_keymap_tree(&kp_audiodw, func_audiodw);
		update_tw_keymap_tree(&kp_audioup, func_audioup);
		update_tw_keymap_tree(&kp_audiopl, func_audio);
		update_tw_keymap_tree(&kp_audiops, func_audio);
		update_tw_keymap_tree(&kp_audionx, func_audio);
		update_tw_keymap_tree(&kp_audiopv, func_audio);
		update_tw_keymap_tree(&kp_ro, func_ro);
//		debug_keybindtree();
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		seat0->pointer = wl_seat_get_pointer(wl_seat);
		fprintf(stderr, "got a mouse\n");
	}
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		seat0->touch = wl_seat_get_touch(wl_seat);
		fprintf(stderr, "got a touchpad\n");
	}
}

static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	struct seat *seat0 = (struct seat *)data;
	fprintf(stderr, "we have this seat with a name called %s\n", name);
	seat0->name = name;
}

static
struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};



////shell with the input
static struct wl_shell *gshell;
static struct wl_compositor *gcompositor;
struct wl_shm *shm;



static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    //struct display *d = data;
    //	d->formats |= (1 << format);
    fprintf(stderr, "Format %d\n", format);
}

static struct wl_shm_listener shm_listener = {
	shm_format
};

static
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
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		fprintf(stderr, "announcing the shell\n");
		gshell = wl_registry_bind(wl_registry, name, &wl_shell_interface, version);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		fprintf(stderr, "announcing the compositor\n");
		gcompositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)  {
		fprintf(stderr, "got a shm handler\n");
		shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
		wl_shm_add_listener(shm, &shm_listener, NULL);
	}


}
static
void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{
	if (name == seat0.id) {
		fprintf(stderr, "wl_seat removed");
	}

}


static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};

static
void
handle_ping(void *data,
		     struct wl_shell_surface *wl_shell_surface,
		     uint32_t serial)
{
	fprintf(stderr, "ping!!!\n");
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
		 uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

struct wl_shell_surface_listener pingpong = {
	.ping = handle_ping,
	.configure = handle_configure,
	.popup_done = handle_popup_done
};

static struct wl_buffer *
create_buffer(int WIDTH, int HEIGHT)
{
	struct wl_shm_pool *pool;
	int stride = WIDTH * 4; // rgba pixels, weston seems have another idea...
	int size = stride * HEIGHT;
	struct wl_buffer *buff;

	struct anonymous_buff_t *buffer = malloc(sizeof(*buffer));
	if ( anonymous_buff_new(buffer, size) < 0) {
		fprintf(stderr, "creating a buffer file for %d failed\n",
			size);
		exit(1);
	}
	void *shm_data = anonymous_buff_alloc(buffer, size, PROT_READ | PROT_WRITE, MAP_SHARED);
	if (!shm_data) {
		fprintf(stderr, "come on man...\n");
		anonymous_buff_close_file(buffer);
		return NULL;
	}
	fprintf(stderr, "the fd is %d\n", buffer->fd);
	pool = wl_shm_create_pool(shm, buffer->fd, size);
	buff = wl_shm_pool_create_buffer(pool, 0,
					 WIDTH, HEIGHT,
					 stride,
					 WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	wl_buffer_set_user_data(buff, buffer);
	return buff;
}


//TODO:
// 1. read a bunch of key configure, or make up some keysequences
// 2. add the configure into the

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

	//okay, now we need to create surface
	struct wl_surface *surface = wl_compositor_create_surface(gcompositor);
	if (!surface) {
		fprintf(stderr, "cant creat a surface\n");
		return -1;
	}
	struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(gshell, surface);
	wl_shell_surface_add_listener(shell_surface, &pingpong, NULL);
	wl_shell_surface_set_toplevel(shell_surface);
	struct wl_buffer *buffer = create_buffer(400, 200);
	//init cario surface
//	cairo_create(cairo_surface_t *target)

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	//allright, now we just need to create that stupid input struct


	while(wl_display_dispatch(display) != -1);

	wl_display_disconnect(display);
	return 0;
}
