/*
 * virtual_keyboard.c - taiwins zwp_virtual_keyboard implementaton
 *
 * Copyright (c) 2021 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/virtual_keyboard.h>
#include <taiwins/objects/seat.h>
#include <wayland-virtual-keyboard-server-protocol.h>

#define VIRTUAL_KEYBOARD_VERSION 1

static inline struct tw_virtual_keyboard *
tw_virtual_keyboard_from_resource(struct wl_resource *resource);

static void
handle_virtual_keyboard_keymap(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t format,
                               int32_t fd,
                               uint32_t size)
{
	struct tw_virtual_keyboard *vkeyboard =
		tw_virtual_keyboard_from_resource(resource);
	if (vkeyboard) {
		struct xkb_keymap *keymap = NULL;
		struct xkb_context *context = NULL;
		struct tw_keyboard *keyboard = &vkeyboard->seat->keyboard;
		char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

		if (data == MAP_FAILED)
			goto mmap_failed;
		context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!context)
			goto context_failed;
		keymap =  xkb_keymap_new_from_string(
			context, data, XKB_KEYMAP_FORMAT_TEXT_V1,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (!keymap)
			goto keymap_failed;
		//compare the keymap, set keymap only when there is difference,
		//so we may avoid the infinite loop
		if (size != keyboard->keymap_size ||
		    strncmp(data, keyboard->keymap_string, size) != 0)
			tw_keyboard_set_keymap(keyboard, keymap);
		xkb_keymap_unref(keymap);
	keymap_failed:
		xkb_context_unref(context);
	context_failed:
		munmap(data, size);

	}

mmap_failed:
	close(fd);
}

static void
handle_virtual_keyboard_key(struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t time,
                            uint32_t key,
                            uint32_t state)
{
	struct tw_virtual_keyboard *keyboard =
		tw_virtual_keyboard_from_resource(resource);
	if (keyboard)
		tw_keyboard_default_key(&keyboard->seat->keyboard.default_grab,
		                        time, key, state);
}
static void
handle_virtual_keyboard_modifiers(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t mods_depressed,
                                  uint32_t mods_latched,
                                  uint32_t mods_locked,
                                  uint32_t group)
{
	struct tw_virtual_keyboard *keyboard =
		tw_virtual_keyboard_from_resource(resource);
	if (keyboard)
		tw_keyboard_default_modifiers(
			&keyboard->seat->keyboard.default_grab,
			mods_depressed, mods_latched, mods_locked, group);
}


static struct zwp_virtual_keyboard_v1_interface keyboard_impl = {
	.key = handle_virtual_keyboard_key,
	.keymap = handle_virtual_keyboard_keymap,
	.modifiers = handle_virtual_keyboard_modifiers,
	.destroy = tw_resource_destroy_common,
};

static inline struct tw_virtual_keyboard *
tw_virtual_keyboard_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_virtual_keyboard_v1_interface,
	                               &keyboard_impl));
	return wl_resource_get_user_data(resource);
}

static void
tw_virtual_keyboard_fini(struct tw_virtual_keyboard *keyboard)
{
	wl_resource_set_user_data(keyboard->resource, NULL); //cleanup

	tw_reset_wl_list(&keyboard->listeners.mgr_destroy.link);
	tw_reset_wl_list(&keyboard->listeners.seat_destroy.link);
	wl_signal_emit(&keyboard->destroy_signal, keyboard);
	keyboard->resource = NULL;
	keyboard->seat = NULL;
}

static void
handle_destroy_keyboard_resource(struct wl_resource *resource)
{
	struct tw_virtual_keyboard *keyboard =
		tw_virtual_keyboard_from_resource(resource);
	//if we not destroyed before resource destroy
	if (keyboard) {
		tw_virtual_keyboard_fini(keyboard);
		free(keyboard);
	}
}

static void
notify_keyboard_seat_destroy(struct wl_listener *listener, void *data)
{
	struct tw_virtual_keyboard *keyboard =
		wl_container_of(listener, keyboard, listeners.seat_destroy);

	tw_virtual_keyboard_fini(keyboard);
	free(keyboard);
}

static void
notify_keyboard_mgr_destroy(struct wl_listener *listener, void *data)
{
	struct tw_virtual_keyboard *keyboard =
		wl_container_of(listener, keyboard, listeners.mgr_destroy);

	tw_virtual_keyboard_fini(keyboard);
	free(keyboard);
}

static void
tw_virtual_keyboard_init(struct tw_virtual_keyboard *keyboard,
                         struct wl_resource *resource,
                         struct wl_resource *mgr_resource,
                         struct tw_seat *seat)
{
	keyboard->seat = seat;
	keyboard->resource = resource;

	wl_signal_init(&keyboard->destroy_signal);

	tw_signal_setup_listener(&seat->signals.destroy,
	                         &keyboard->listeners.seat_destroy,
	                         notify_keyboard_seat_destroy);
	tw_set_resource_destroy_listener(mgr_resource,
	                                 &keyboard->listeners.mgr_destroy,
	                                 notify_keyboard_mgr_destroy);
	//ensure we have a keyboard in the seat
	tw_seat_new_keyboard(seat);
}

static void
handle_create_virtual_keyboard(struct wl_client *client,
                               struct wl_resource *manager_resource,
                               struct wl_resource *wl_seat,
                               uint32_t id)
{
	struct tw_virtual_keyboard *keyboard;
	struct wl_resource *resource = NULL;
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_virtual_keyboard_manager *mgr =
		wl_resource_get_user_data(manager_resource);
	struct tw_seat *seat = tw_seat_from_resource(wl_seat);

	if (!tw_create_wl_resource_for_obj(resource, keyboard,
	                                   client, id, version,
	                                   zwp_virtual_keyboard_v1_interface)){
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	tw_virtual_keyboard_init(keyboard, resource, manager_resource, seat);
	wl_resource_set_implementation(resource, &keyboard_impl, keyboard,
	                               handle_destroy_keyboard_resource);
	wl_signal_emit(&mgr->new_keyboard, keyboard);
}

static const struct zwp_virtual_keyboard_manager_v1_interface mgr_impl = {
	.create_virtual_keyboard = handle_create_virtual_keyboard,
};

static void
bind_virtual_keyboard_manager(struct wl_client *client, void *data,
                              uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &zwp_virtual_keyboard_manager_v1_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &mgr_impl, data, NULL);
}

static void
notify_mgr_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_virtual_keyboard_manager *mgr =
		wl_container_of(listener, mgr, display_destroy);

        wl_list_remove(&listener->link);
	wl_global_destroy(mgr->global);
	mgr->global = NULL;
}

WL_EXPORT bool
tw_virtual_keyboard_manager_init(struct tw_virtual_keyboard_manager *mgr,
                                 struct wl_display *display)
{
	if (!(mgr->global =
	      wl_global_create(display,
	                       &zwp_virtual_keyboard_manager_v1_interface,
	                       VIRTUAL_KEYBOARD_VERSION, mgr,
	                       bind_virtual_keyboard_manager)))
		return false;
	wl_signal_init(&mgr->new_keyboard);
	tw_set_display_destroy_listener(display, &mgr->display_destroy,
	                                notify_mgr_display_destroy);

	return false;
}

WL_EXPORT struct tw_virtual_keyboard_manager *
tw_virtual_keyboard_manager_create_global(struct wl_display *display)
{
	static struct tw_virtual_keyboard_manager mgr = {0};

	if (mgr.global)
		return &mgr;
	if (!tw_virtual_keyboard_manager_init(&mgr, display))
		return NULL;
	return &mgr;
}
