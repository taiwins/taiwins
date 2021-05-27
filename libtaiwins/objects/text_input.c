/*
 * text_input.c - taiwins server text input implementation
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
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/text_input.h>
#include <taiwins/objects/input_method.h>
#include <wayland-text-input-server-protocol.h>
#include <wayland-util.h>

static const struct zwp_text_input_v3_interface text_input_impl;

static inline struct tw_text_input *
text_input_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &zwp_text_input_v3_interface,
	                               &text_input_impl));
	return wl_resource_get_user_data(resource);
}

static void
handle_text_input_enable(struct wl_client *client,
                         struct wl_resource *resource)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	struct tw_text_input_state reset = {0};
	if (!ti)
		return;
	//reset pending state
	free(ti->pending.surrounding.text);
	ti->pending = reset;
	ti->pending.enabled = true;
	ti->pending.requests |= TW_TEXT_INPUT_TOGGLE;
	ti->pending.focused = ti->focused;
}

static void
handle_text_input_disable(struct wl_client *client,
                          struct wl_resource *resource)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;
	ti->pending.enabled = false;
	ti->pending.requests |= TW_TEXT_INPUT_TOGGLE;
}

static void
handle_text_input_set_surrounding_text(struct wl_client *client,
                                       struct wl_resource *resource,
                                       const char *text,
                                       int32_t cursor,
                                       int32_t anchor)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;
	free(ti->pending.surrounding.text);
	ti->pending.surrounding.text = strdup(text);
	if (!ti->pending.surrounding.text)
		wl_resource_post_no_memory(resource);
	ti->pending.surrounding.cursor = cursor;
	ti->pending.surrounding.anchor = anchor;
	ti->pending.requests |= TW_TEXT_INPUT_SURROUNDING_TEXT;
}

static void
handle_text_input_set_text_change_cause(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t cause)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;
	ti->pending.change_cause = cause;
	ti->pending.requests |= TW_TEXT_INPUT_CHANGE_CAUSE;
}

static void
handle_text_input_set_content_type(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t hint,
                                   uint32_t purpose)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;
	ti->pending.content_hint = hint;
	ti->pending.content_purpose = purpose;
	ti->pending.requests |= TW_TEXT_INPUT_CONTENT_TYPE;
}

static void
handle_text_input_set_cursor_rectangle(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;

	ti->pending.cursor_rect.x = x;
	ti->pending.cursor_rect.y = y;
	ti->pending.cursor_rect.width = width;
	ti->pending.cursor_rect.height = height;
	ti->pending.requests |= TW_TEXT_INPUT_CURSOR_RECTANGLE;
}

static void
handle_text_input_commit(struct wl_client *client,
                         struct wl_resource *resource)
{
	struct tw_text_input_state reset = {0};
	struct tw_seat *seat;
	struct tw_input_method *im;
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (!ti)
		return;

	//sending event to input method
	seat = ti->seat;
	im = tw_input_method_find_from_seat(seat);
	if (im) {
		struct tw_input_method_event e = {0};

		if (ti->pending.requests & TW_TEXT_INPUT_TOGGLE) {
			e.events |= TW_INPUT_METHOD_TOGGLE;
			e.enabled = ti->pending.enabled;
			e.focused = ti->pending.focused;
		}
		if (ti->pending.requests & TW_TEXT_INPUT_SURROUNDING_TEXT) {
			e.events |= TW_INPUT_METHOD_SURROUNDING_TEXT;
			e.surrounding.anchor = ti->pending.surrounding.anchor;
			e.surrounding.cursor = ti->pending.surrounding.cursor;
			e.surrounding.text = ti->pending.surrounding.text;
		}
		if (ti->pending.requests & TW_TEXT_INPUT_CHANGE_CAUSE) {
			e.events |= TW_INPUT_METHOD_CHANGE_CAUSE;
			e.change_cause = ti->pending.change_cause;
		}
		if (ti->pending.requests & TW_TEXT_INPUT_CONTENT_TYPE) {
			e.events |= TW_INPUT_METHOD_CONTENT_TYPE;
			e.content_hint = ti->pending.content_hint;
			e.content_purpose = ti->pending.content_purpose;
		}
		if (ti->pending.requests & TW_TEXT_INPUT_CURSOR_RECTANGLE) {
			e.events |= TW_INPUT_METHOD_CURSOR_RECTANGLE;
			e.cursor_rect.x = ti->pending.cursor_rect.x;
			e.cursor_rect.y = ti->pending.cursor_rect.y;
			e.cursor_rect.width = ti->pending.cursor_rect.width;
			e.cursor_rect.height = ti->pending.cursor_rect.height;
		}

		tw_input_method_send_event(im, &e);
	}

        //swap state
	free(ti->current.surrounding.text);
	ti->current = ti->pending;
	ti->pending = reset;
	ti->serial++;
}

static const struct zwp_text_input_v3_interface text_input_impl = {
	.destroy = tw_resource_destroy_common,
	.enable = handle_text_input_enable,
	.disable = handle_text_input_disable,
	.set_surrounding_text = handle_text_input_set_surrounding_text,
	.set_text_change_cause = handle_text_input_set_text_change_cause,
	.set_content_type = handle_text_input_set_content_type,
	.set_cursor_rectangle = handle_text_input_set_cursor_rectangle,
	.commit = handle_text_input_commit,
};

static void
notify_text_input_focus(struct wl_listener *listener, void *data)
{
	struct wl_resource *focused = NULL;
	struct tw_text_input *ti =
		wl_container_of(listener, ti, focus_listener);
	struct tw_input_method *im =
		tw_input_method_find_from_seat(ti->seat);
	//skip if there is no input method or this is not a keyboard focus
	if (!im || data != &ti->seat->keyboard)
		return;
	focused = ti->seat->keyboard.focused_surface;

	if (ti->focused && ti->focused != focused) {
		zwp_text_input_v3_send_leave(ti->resource, ti->focused);
		ti->focused = NULL;
	}
	// the focused surface of text input should be from text_input
	if (!tw_match_wl_resource_client(focused, ti->resource))
		return;
	if (tw_match_wl_resource_client(ti->resource, focused)) {
		zwp_text_input_v3_send_enter(ti->resource, focused);
		ti->focused = focused;
	}
}

static void
destroy_text_input_resource(struct wl_resource *resource)
{
	struct tw_text_input *ti = text_input_from_resource(resource);
	if (ti) {
		free(ti->pending.surrounding.text);
		free(ti->current.surrounding.text);
		free(ti);
	}
	wl_list_remove(&ti->focus_listener.link);
	wl_list_remove(wl_resource_get_link(resource));
	wl_resource_set_user_data(resource, NULL);
}

/******************************************************************************
 * text_input_manager implemenation
 *****************************************************************************/

static void
handle_text_input_manager_get_text_input(struct wl_client *client,
                                         struct wl_resource *manager_resource,
                                         uint32_t id,
                                         struct wl_resource *seat_resource)
{
	struct tw_text_input *text_input;
	struct wl_resource *resource;
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);

	if (!tw_create_wl_resource_for_obj(resource, text_input,
	                                   client, id, version,
	                                   zwp_text_input_v3_interface)) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	text_input->serial = 0;
	text_input->seat = seat;
	text_input->resource = resource;
	tw_signal_setup_listener(&seat->signals.focus,
	                         &text_input->focus_listener,
	                         notify_text_input_focus);

	wl_resource_set_implementation(resource, &text_input_impl, text_input,
	                               destroy_text_input_resource);
	//insert as exotic resource
	wl_list_insert(seat->resources.prev, wl_resource_get_link(resource));
}

static const struct zwp_text_input_manager_v3_interface text_input_man_impl = {
	.destroy = tw_resource_destroy_common,
	.get_text_input = handle_text_input_manager_get_text_input,
};

static void
destroy_text_input_manager_resource(struct wl_resource *resource)
{
	wl_resource_set_user_data(resource, NULL);
}

static void
bind_text_input_manager(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &zwp_text_input_manager_v3_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &text_input_man_impl, data,
	                               destroy_text_input_manager_resource);
}

static inline void
destroy_text_input_manager(struct tw_text_input_manager *manager)
{
	wl_global_destroy(manager->global);
	wl_list_remove(&manager->display_destroy_listener.link);
	manager->global = NULL;
	manager->display = NULL;
}

static void
notify_text_input_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_text_input_manager *manager =
		wl_container_of(listener, manager, display_destroy_listener);
	destroy_text_input_manager(manager);
}

/******************************************************************************
 * public APIs
 *****************************************************************************/

WL_EXPORT void
tw_text_input_commit_event(struct tw_text_input *text_input,
                           struct tw_text_input_event *e)
{
	if (e->events & TW_TEXT_INPUT_PREEDIT)
		zwp_text_input_v3_send_preedit_string(text_input->resource,
		                                      e->preedit.text,
		                                      e->preedit.cursor_begin,
		                                      e->preedit.cursor_end);
	if (e->events & TW_TEXT_INPUT_COMMIT_STRING)
		zwp_text_input_v3_send_commit_string(text_input->resource,
		                                     e->commit_string);
	if (e->events & TW_TEXT_INPUT_SURROUNDING_DELETE)
		zwp_text_input_v3_send_delete_surrounding_text(
			text_input->resource,
			e->surrounding_delete.before_length,
			e->surrounding_delete.after_length);
	if (e->events)
		zwp_text_input_v3_send_done(text_input->resource,
		                            text_input->serial);
}

WL_EXPORT struct tw_text_input *
tw_text_input_find_from_seat(struct tw_seat *seat)
{
	struct tw_text_input *ti = NULL;
	struct wl_resource *res;
	wl_resource_for_each(res, &seat->resources) {
		if (wl_resource_instance_of(res, &zwp_text_input_v3_interface,
		                            &text_input_impl) &&
		    (ti = wl_resource_get_user_data(res))) {
			if (ti->focused == seat->keyboard.focused_surface)
				return ti;
		}

	}
	return NULL;
}

WL_EXPORT bool
tw_text_input_manager_init(struct tw_text_input_manager *manager,
                           struct wl_display *display)
{
	if (!(manager->global ==
	      wl_global_create(display,
	                       &zwp_text_input_manager_v3_interface, 1,
	                       manager, bind_text_input_manager)))
		return false;
	manager->display = display;
	tw_set_display_destroy_listener(display,
	                                &manager->display_destroy_listener,
	                                notify_text_input_display_destroy);

	return true;
}

WL_EXPORT struct tw_text_input_manager *
tw_text_input_manager_create_global(struct wl_display *display)
{
	static struct tw_text_input_manager s_manager = {0};
	assert(s_manager.display == NULL || s_manager.display == display);
	if (s_manager.display == display)
		return &s_manager;
	else if (tw_text_input_manager_init(&s_manager, display))
		return &s_manager;
	return NULL;
}
