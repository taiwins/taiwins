/*
 * input_method.c - taiwins server input-method implementation
 *
 * Copyright (c) 2020 Xichen Zhou
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
#include <wayland-server.h>

#include <taiwins/objects/input_method.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/text_input.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/utils.h>
#include <wayland-input-method-server-protocol.h>
#include "os-compatibility.h"

static const struct zwp_input_method_v2_interface im_v2_impl;
static const struct zwp_input_method_keyboard_grab_v2_interface grab_impl;
static const struct zwp_input_popup_surface_v2_interface popup_impl;

static inline struct tw_input_method *
input_method_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_input_method_v2_interface,
	                               &im_v2_impl));
	return wl_resource_get_user_data(resource);
}

static inline struct tw_input_method *
input_method_from_grab_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(
		       resource, &zwp_input_method_keyboard_grab_v2_interface,
		       &grab_impl));
	return wl_resource_get_user_data(resource);
}

static inline struct tw_input_method *
input_method_from_popup_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_input_popup_surface_v2_interface,
	                               &popup_impl));
	return wl_resource_get_user_data(resource);

}

/******************************************************************************
 * input method popup implemenation
 *****************************************************************************/

#define INPUT_POPUP_ROLE "input_popup"

static void
commit_input_popup_surface(struct tw_surface *surface)
{
	struct tw_input_method *im = surface->role.commit_private;
	struct tw_subsurface *sub = &im->im_surface.subsurface;
	struct tw_surface *parent = sub->parent;

        if (!parent)
		return;
        //set position same as subsurface.
        tw_surface_set_position(sub->surface,
                                sub->parent->geometry.x+sub->sx,
                                sub->parent->geometry.y+sub->sy);
}

static const struct zwp_input_popup_surface_v2_interface popup_impl = {
	.destroy = tw_resource_destroy_common,
};

/**
 * calculate the relative position of the text input from input method surface
 *
 * The text_cursor here is in the parent surface space.
 */
static void
im_popup_calc_pos(struct tw_input_method *im,
                  const pixman_rectangle32_t *text_cursor)
{
	struct tw_subsurface *sub = &im->im_surface.subsurface;
	if (!sub->surface || !sub->resource || !sub->parent)
		return;

	//The simplest case is just set it underneath.
	sub->sx = text_cursor->x + text_cursor->width;
	sub->sy = text_cursor->y + text_cursor->height;
	zwp_input_popup_surface_v2_send_text_input_rectangle(
		sub->resource, -text_cursor->width,
		-text_cursor->height, text_cursor->width,
		text_cursor->height);
}

static void
im_popup_enable(struct tw_input_method *im, struct wl_resource *parent_res)
{
	struct tw_surface *parent = NULL;
	struct tw_subsurface *sub = &im->im_surface.subsurface;

	if (!im->im_surface.subsurface.surface || !parent_res)
		return;
	tw_reset_wl_list(&sub->parent_link);
	tw_reset_wl_list(&sub->parent_pending_link);
	tw_reset_wl_list(&sub->surface_destroyed.link);

	parent = tw_surface_from_resource(parent_res);
	//take effect on text parent commit.
	sub->parent = parent;
	wl_list_insert(&parent->subsurfaces_pending,
	               &sub->parent_pending_link);
	im->active = true;
}

static void
im_popup_disable(struct tw_input_method *im)
{
	struct tw_subsurface *sub = &im->im_surface.subsurface;

	if (!im->im_surface.subsurface.surface)
		return;

	tw_reset_wl_list(&sub->parent_link);
	tw_reset_wl_list(&sub->parent_pending_link);
	tw_reset_wl_list(&sub->surface_destroyed.link);
	sub->parent = NULL;
	sub->sx = 0;
	sub->sy = 0;
	im->active = false;
}

static void
destroy_im_popup_resource(struct wl_resource *resource)
{
	struct tw_surface *popup_surface;
	struct tw_input_method *im =
		input_method_from_popup_resource(resource);

	if (!im)
		return;
	im_popup_disable(im);

	//client should not destroy the surface before the popup destroy, we
	//will notify client for that
	popup_surface = im->im_surface.subsurface.surface;
	if (!popup_surface) {
		wl_resource_post_error(resource, 0, "input method should not "
		                       "destroy surface before destroying "
		                       "input method popup object@%d",
		                       wl_resource_get_id(resource));
	} else {
		popup_surface->role.commit = NULL;
		popup_surface->role.commit_private = NULL;
		popup_surface->role.name = NULL;
	}
	im->im_surface.subsurface.resource = NULL;
	im->im_surface.subsurface.surface = NULL;
}

static void
notify_im_popup_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct tw_subsurface *sub =
		wl_container_of(listener, sub, surface_destroyed);
	struct tw_input_method *im =
		wl_container_of(sub, im, im_surface.subsurface);

        wl_list_remove(&sub->surface_destroyed.link);
	im_popup_disable(im);
	im->im_surface.subsurface.resource = NULL;
	im->im_surface.subsurface.surface = NULL;
}

static void
input_method_impl_subsurface(struct tw_input_method *im,
                             struct tw_surface *surface,
                             struct wl_resource *resource)
{
	struct tw_subsurface *sub = &im->im_surface.subsurface;

	sub->resource = resource;
	sub->surface = surface;
	sub->sync = false;
	sub->parent = NULL;
	sub->sx = 0;
	sub->sy = 0;

	//this surface has a popup role.
	surface->role.commit = commit_input_popup_surface;
	surface->role.commit_private = im;
	surface->role.name = INPUT_POPUP_ROLE;

	tw_signal_setup_listener(&surface->signals.destroy,
	                         &sub->surface_destroyed,
	                         notify_im_popup_surface_destroyed);
}

/******************************************************************************
 * input method keyboard implemenation
 *****************************************************************************/

static const struct zwp_input_method_keyboard_grab_v2_interface grab_impl = {
	.release = tw_resource_destroy_common,
};

static void
notify_im_keyboard_key(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
                       uint32_t key, uint32_t state)
{
	struct wl_display *display = grab->seat->display;
	uint32_t serial = wl_display_next_serial(display);

	zwp_input_method_keyboard_grab_v2_send_key(grab->data, serial,
	                                           time_msec, key, state);
}

static void
notify_im_keyboard_modifiers(struct tw_seat_keyboard_grab *grab,
                             uint32_t mods_depressed, uint32_t mods_latched,
                             uint32_t mods_locked, uint32_t group)
{
	struct wl_display *display = grab->seat->display;
	uint32_t serial = wl_display_next_serial(display);

	zwp_input_method_keyboard_grab_v2_send_modifiers(grab->data, serial,
	                                                 mods_depressed,
	                                                 mods_latched,
	                                                 mods_locked, group);
}

static const struct tw_keyboard_grab_interface im_grab = {
	.key = notify_im_keyboard_key,
	.modifiers = notify_im_keyboard_modifiers,
};

static void
input_method_send_keymap(struct tw_input_method *im,
                         struct tw_keyboard *keyboard,
                         struct wl_resource *grab_resource)
{
	int keymap_fd;
	void *ptr;

	//this would later be a problem for virtual keyboards to involving
	//looping keymap sending.
	if (keyboard->keymap_string) {
		keymap_fd = os_create_anonymous_file(keyboard->keymap_size);
		if (!keymap_fd)
			return;
		ptr = mmap(NULL, keyboard->keymap_size, PROT_READ | PROT_WRITE,
		           MAP_SHARED, keymap_fd, 0);
		if (ptr == MAP_FAILED)
			goto close_fd;
		strcpy(ptr, keyboard->keymap_string);
		zwp_input_method_keyboard_grab_v2_send_keymap(
			grab_resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
			keymap_fd, keyboard->keymap_size);
		munmap(ptr, keyboard->keymap_size);
	close_fd:
		close(keymap_fd);
	}
}

static void
input_method_start_grab(struct tw_input_method *im,
                        struct tw_keyboard *keyboard,
                        struct wl_resource *grab_resource)
{
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

	im->im_grab.data = grab_resource;
	im->im_grab.impl = &im_grab;

	input_method_send_keymap(im, keyboard, grab_resource);
	zwp_input_method_keyboard_grab_v2_send_repeat_info(grab_resource,
	                                                   seat->repeat_rate,
	                                                   seat->repeat_delay);

	tw_keyboard_start_grab(keyboard, &im->im_grab);
}

static void
handle_destroy_im_grab_resource(struct wl_resource *resource)
{
	struct tw_input_method *im = input_method_from_grab_resource(resource);
	struct tw_seat *seat = im->seat;

	tw_keyboard_end_grab(&seat->keyboard);
}

/******************************************************************************
 * input method implemenation
 *****************************************************************************/

static void
handle_input_method_commit_string(struct wl_client *client,
                                  struct wl_resource *resource,
                                  const char *text)
{
	struct tw_input_method *im = input_method_from_resource(resource);
	if (!im)
		return;
	free(im->pending.commit_string);
	im->pending.commit_string = strdup(text);

	if (!im->pending.commit_string)
		wl_resource_post_no_memory(resource);

	im->pending.requests |= TW_INPUT_METHOD_COMMIT_STRING;
}

static void
handle_input_method_set_preedit_string(struct wl_client *client,
				   struct wl_resource *resource,
				   const char *text,
				   int32_t cursor_begin,
				   int32_t cursor_end)
{
	struct tw_input_method *im = input_method_from_resource(resource);
	if (!im)
		return;
	free(im->pending.preedit.text);
	im->pending.preedit.text = strdup(text);

	if (!im->pending.preedit.text)
		wl_resource_post_no_memory(resource);

	im->pending.preedit.cursor_begin = cursor_begin;
	im->pending.preedit.cursor_end = cursor_end;
	im->pending.requests |= TW_INPUT_METHOD_PREEDIT;
}

static void
handle_input_method_delete_surrounding_text(struct wl_client *client,
                                             struct wl_resource *resource,
                                             uint32_t before_length,
                                             uint32_t after_length)
{
	struct tw_input_method *im = input_method_from_resource(resource);
	if (!im)
		return;
	im->pending.surrounding_delete.before_length = before_length;
	im->pending.surrounding_delete.after_length = after_length;
	im->pending.requests |= TW_INPUT_METHOD_SURROUNDING_DELETE;
}

static void
handle_input_method_commit(struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t serial)
{
	struct tw_text_input *ti = NULL;
	struct tw_input_method *im = input_method_from_resource(resource);
	struct tw_input_method_state reset = {0};
	if (!im)
		return;
	//sending events.
	if ((ti = tw_text_input_find_from_seat(im->seat))) {
		struct tw_text_input_event e;

		if (im->pending.requests & TW_INPUT_METHOD_PREEDIT) {
			e.preedit.text = im->pending.preedit.text;
			e.preedit.cursor_begin =
				im->pending.preedit.cursor_begin;
			e.preedit.cursor_end =
				im->pending.preedit.cursor_end;
			e.events |= TW_TEXT_INPUT_PREEDIT;
		}
		if (im->pending.requests & TW_INPUT_METHOD_COMMIT_STRING) {
			e.commit_string = im->pending.commit_string;
			e.events |= TW_TEXT_INPUT_COMMIT_STRING;
		}
		if (im->pending.requests & TW_INPUT_METHOD_SURROUNDING_DELETE){
			e.surrounding_delete.before_length =
				im->pending.surrounding_delete.before_length;
			e.surrounding_delete.after_length =
				im->pending.surrounding_delete.after_length;
			e.events |= TW_TEXT_INPUT_SURROUNDING_DELETE;
		}
		//TODO this serial is not right
		e.serial = serial;

		tw_text_input_commit_event(ti, &e);
	}

	//swap state
	free(im->current.commit_string);
	free(im->current.preedit.text);
	im->current = im->pending;
	im->pending = reset;
}

static void
handle_input_method_get_input_popup_surface(struct wl_client *client,
                                            struct wl_resource *resource,
                                            uint32_t id,
                                            struct wl_resource *surf_resource)
{
	struct wl_resource *popup_resource;
	struct tw_input_method *im = input_method_from_resource(resource);
	uint32_t version = wl_resource_get_version(resource);
	struct tw_surface *surface = tw_surface_from_resource(surf_resource);

	if (tw_surface_has_role(surface)) {
		wl_resource_post_error(resource, 0,
		                       "wl_surface@%d has another role",
		                       wl_resource_get_id(surf_resource));
		return;
	}
	if (!im)
		return;

	popup_resource =
		wl_resource_create(client,
		                   &zwp_input_popup_surface_v2_interface,
		                   version, id);
	if (!popup_resource) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(popup_resource, &popup_impl, im,
	                               destroy_im_popup_resource);

	input_method_impl_subsurface(im, surface, popup_resource);
}

static void
handle_input_method_grab_keyboard(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t keyboard)
{
	struct tw_seat *seat;
	struct wl_resource *grab_resource = NULL;
	uint32_t version = wl_resource_get_version(resource);
	struct tw_input_method *im = input_method_from_resource(resource);

	if (!im)
		return;
	seat = im->seat;
	//no keyboard
	if (!(seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD))
		return;
	//already in grab
	if (seat->keyboard.grab == &im->im_grab ||
	    seat->keyboard.grab != &seat->keyboard.default_grab)
		return;
	if (!(grab_resource =
	      wl_resource_create(client,
	                         &zwp_input_method_keyboard_grab_v2_interface,
	                         version, keyboard))) {
		wl_resource_post_no_memory(resource);
		return;
	}
	//a keyboard grab.
	wl_resource_set_implementation(grab_resource, &grab_impl, im,
	                               handle_destroy_im_grab_resource);
	input_method_start_grab(im, &seat->keyboard, grab_resource);
}

static const struct zwp_input_method_v2_interface im_v2_impl = {
	.commit_string = handle_input_method_commit_string,
	.set_preedit_string = handle_input_method_set_preedit_string,
	.delete_surrounding_text = handle_input_method_delete_surrounding_text,
	.commit = handle_input_method_commit,
	.get_input_popup_surface = handle_input_method_get_input_popup_surface,
	.grab_keyboard = handle_input_method_grab_keyboard,
	.destroy = tw_resource_destroy_common,
};

static void
destroy_input_method(struct tw_input_method *im)
{
	wl_list_remove(&im->seat_destroy_listener.link);
	wl_list_remove(wl_resource_get_link(im->resource));
	wl_list_remove(&im->link);
	wl_resource_set_user_data(im->resource, NULL);
	free(im);
}

static void
destroy_input_method_resource(struct wl_resource *resource)
{
	struct tw_input_method *im = input_method_from_resource(resource);
	if (!im)
		return;
	destroy_input_method(im);
}

static void
notify_im_v2_seat_destroy(struct wl_listener *listener, void *data)
{
	struct tw_input_method *im =
		wl_container_of(listener, im, seat_destroy_listener);
	zwp_input_method_v2_send_unavailable(im->resource);
	destroy_input_method(im);
}

/******************************************************************************
 * input method manager
 *****************************************************************************/

static const struct zwp_input_method_manager_v2_interface im_manager_impl;

static inline struct tw_input_method_manager *
manager_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_input_method_manager_v2_interface,
	                               &im_manager_impl));
	return wl_resource_get_user_data(resource);
}

static void
im_manager_handle_get_input_method(struct wl_client *client,
                                   struct wl_resource *manager_resource,
                                   struct wl_resource *seat_resource,
                                   uint32_t input_method)
{
	struct tw_input_method *im;
	struct wl_resource *im_resource = NULL;
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);
	struct tw_input_method_manager *manager =
		manager_from_resource(manager_resource);


	if (!tw_create_wl_resource_for_obj(im_resource, im, client,
	                                   input_method, version,
	                                   zwp_input_method_v2_interface)) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	im->resource = im_resource;
	im->seat = seat;
	wl_list_init(&im->link);
	wl_resource_set_implementation(im_resource, &im_v2_impl, im,
	                               destroy_input_method_resource);
	tw_signal_setup_listener(&seat->destroy_signal,
	                         &im->seat_destroy_listener,
	                         notify_im_v2_seat_destroy);
	//as described in protocol, if we have an input method already in the
	//seat, the only thing we need to do is sending unavailable event
	if (tw_input_method_find_from_seat(seat)) {
		zwp_input_method_v2_send_unavailable(im->resource);
		destroy_input_method(im);
		return;
	}
	wl_list_insert(manager->ims.prev, &im->link);
	wl_list_insert(seat->resources.prev,
	               wl_resource_get_link(im_resource));
}

static const struct zwp_input_method_manager_v2_interface im_manager_impl = {
	.get_input_method = im_manager_handle_get_input_method,
	.destroy = tw_resource_destroy_common,
};

static void
bind_input_method_manager(struct wl_client *wl_client, void *data,
                          uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(wl_client,
		                   &zwp_input_method_manager_v2_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource, &im_manager_impl, data,
	                               NULL);
}

static void
notify_im_manager_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_input_method_manager *manager =
		wl_container_of(listener, manager, display_destroy_listener);

	assert(data == manager->display);
	wl_global_destroy(manager->global);
	manager->global = NULL;
	manager->display = NULL;
	tw_reset_wl_list(&listener->link);
}

WL_EXPORT void
tw_input_method_send_event(struct tw_input_method *im,
                           struct tw_input_method_event *e)
{
	if ((e->events & TW_INPUT_METHOD_TOGGLE) && e->enabled) {
		zwp_input_method_v2_send_activate(im->resource);
		im_popup_enable(im, e->focused);
	} else if (e->events & TW_INPUT_METHOD_TOGGLE) {
		zwp_input_method_v2_send_deactivate(im->resource);
		im_popup_disable(im);
	}

	if (e->events & TW_INPUT_METHOD_SURROUNDING_TEXT)
		zwp_input_method_v2_send_surrounding_text(
			im->resource, e->surrounding.text,
			e->surrounding.cursor, e->surrounding.anchor);
	if (e->events & TW_INPUT_METHOD_CHANGE_CAUSE)
		zwp_input_method_v2_send_text_change_cause(im->resource,
		                                           e->change_cause);
	if (e->events & TW_INPUT_METHOD_CONTENT_TYPE)
		zwp_input_method_v2_send_content_type(im->resource,
		                                      e->content_hint,
		                                      e->content_purpose);
	if (e->events & TW_INPUT_METHOD_CURSOR_RECTANGLE) {
		//sending rectanges if it there
		im_popup_calc_pos(im, &e->cursor_rect);
		im->im_surface.rectangle = e->cursor_rect;
	}
	zwp_input_method_v2_send_done(im->resource);
}

WL_EXPORT struct tw_input_method *
tw_input_method_find_from_seat(struct tw_seat *seat)
{
	struct tw_input_method *method;
	struct wl_resource *res;

	wl_resource_for_each(res, &seat->resources) {
		if (wl_resource_instance_of(res,
		                            &zwp_input_method_v2_interface,
		                            &im_v2_impl) &&
		    (method = input_method_from_resource(res)))
			return method;
	}
	return NULL;
}

WL_EXPORT bool
tw_input_method_manager_init(struct tw_input_method_manager *manager,
                             struct wl_display *display)
{
	if (!(manager->global =
	      wl_global_create(display, &zwp_input_method_manager_v2_interface,
	                       1, manager, bind_input_method_manager)))
		return false;

	manager->display = display;
	tw_set_display_destroy_listener(display,
	                                &manager->display_destroy_listener,
	                                notify_im_manager_display_destroy);
	wl_list_init(&manager->ims);

	return true;
}

WL_EXPORT struct tw_input_method_manager *
tw_input_method_manager_create_global(struct wl_display *display)
{
	static struct tw_input_method_manager s_manager = {0};
	assert(s_manager.display == NULL || s_manager.display == display);
	if (s_manager.display == display)
		return &s_manager;
	else if (tw_input_method_manager_init(&s_manager, display))
		return &s_manager;
	return NULL;
}
