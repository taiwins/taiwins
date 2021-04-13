/*
 * gestures.c - taiwins wp_pointer_gestures implementation
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
#include <taiwins/objects/utils.h>
#include <taiwins/objects/gestures.h>
#include <taiwins/objects/seat.h>
#include <wayland-pointer-gestures-server-protocol.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#define GESTURES_VERSION 2

static const struct zwp_pointer_gesture_pinch_v1_interface pinch_impl = {
	.destroy = tw_resource_destroy_common,
};

WL_EXPORT void
tw_gestures_pinch_begin(struct tw_gestures_manager *manager,
                        struct tw_pointer *pointer, uint32_t time,
                        struct wl_resource *surface, uint32_t fingers)
{
	struct wl_resource *pinch_res;
	uint32_t serial = wl_display_next_serial(manager->display);

	wl_resource_for_each(pinch_res, &manager->pinchs) {
		struct tw_pointer *swipe_pointer =
			wl_resource_get_user_data(pinch_res);
		if (swipe_pointer == pointer)
			zwp_pointer_gesture_pinch_v1_send_begin(
				pinch_res, serial, time, surface, fingers);
	}
}

WL_EXPORT void
tw_gestures_pinch_update(struct tw_gestures_manager *manager,
                         struct tw_pointer *pointer, uint32_t time,
                         double dx, double dy, double scale, double rotation)
{
	struct wl_resource *pinch_res;
	wl_fixed_t dxf = wl_fixed_from_double(dx);
	wl_fixed_t dyf = wl_fixed_from_double(dy);
	wl_fixed_t sf = wl_fixed_from_double(scale);
	wl_fixed_t rf = wl_fixed_from_double(rotation);

	wl_resource_for_each(pinch_res, &manager->pinchs) {
		struct tw_pointer *pinch_pointer =
			wl_resource_get_user_data(pinch_res);
		if (pinch_pointer == pointer)
			zwp_pointer_gesture_pinch_v1_send_update(
				pinch_res, time, dxf, dyf, sf, rf);
	}
}

WL_EXPORT void
tw_gestures_pinch_end(struct tw_gestures_manager *manager,
                      struct tw_pointer *pointer, uint32_t time, bool cancel)
{
	struct wl_resource *pinch_res;
	uint32_t serial = wl_display_next_serial(manager->display);

        wl_resource_for_each(pinch_res, &manager->pinchs) {
		struct tw_pointer *pinch_pointer =
			wl_resource_get_user_data(pinch_res);
		if (pinch_pointer == pointer)
			zwp_pointer_gesture_pinch_v1_send_end(
				pinch_res, serial, time, cancel);
	}
}

/******************************************************************************
 * swipe interface
 *****************************************************************************/

static const struct zwp_pointer_gesture_swipe_v1_interface swipe_impl = {
	.destroy = tw_resource_destroy_common,
};

WL_EXPORT void
tw_gestures_swipe_begin(struct tw_gestures_manager *manager,
                        struct tw_pointer *pointer, uint32_t time,
                        struct wl_resource *surface, uint32_t fingers)
{
	struct wl_resource *swipe_res;
	uint32_t serial = wl_display_next_serial(manager->display);

	wl_resource_for_each(swipe_res, &manager->swipes) {
		struct tw_pointer *swipe_pointer =
			wl_resource_get_user_data(swipe_res);
		if (swipe_pointer == pointer)
			zwp_pointer_gesture_swipe_v1_send_begin(
				swipe_res, serial, time, surface, fingers);
	}
}

WL_EXPORT void
tw_gestures_swipe_update(struct tw_gestures_manager *manager,
                         struct tw_pointer *pointer, uint32_t time,
                         double dx, double dy)
{
	struct wl_resource *swipe_res;
	wl_fixed_t dxf = wl_fixed_from_double(dx);
	wl_fixed_t dyf = wl_fixed_from_double(dy);

	wl_resource_for_each(swipe_res, &manager->swipes) {
		struct tw_pointer *swipe_pointer =
			wl_resource_get_user_data(swipe_res);
		if (swipe_pointer == pointer)
			zwp_pointer_gesture_swipe_v1_send_update(
				swipe_res, time, dxf, dyf);
	}
}

WL_EXPORT void
tw_gestures_swipe_end(struct tw_gestures_manager *manager,
                      struct tw_pointer *pointer, uint32_t time, bool cancel)
{
	struct wl_resource *swipe_res;
	uint32_t serial = wl_display_next_serial(manager->display);

        wl_resource_for_each(swipe_res, &manager->swipes) {
		struct tw_pointer *swipe_pointer =
			wl_resource_get_user_data(swipe_res);
		if (swipe_pointer == pointer)
			zwp_pointer_gesture_swipe_v1_send_end(
				swipe_res, serial, time, cancel);
	}
}


static void
handle_gesture_resource_destroy(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}


/******************************************************************************
 * gestures implementation
 *****************************************************************************/

static const struct zwp_pointer_gestures_v1_interface gestures_impl;

static inline struct tw_gestures_manager *
tw_gestures_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_pointer_gestures_v1_interface,
	                               &gestures_impl));
	return wl_resource_get_user_data(resource);
}

static void
handle_gestures_get_swipe_gesture(struct wl_client *client,
                                  struct wl_resource *manager_resource,
                                  uint32_t id,
                                  struct wl_resource *pointer_resource)
{
	struct tw_gestures_manager *manager =
		tw_gestures_from_resource(manager_resource);
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &zwp_pointer_gesture_swipe_v1_interface,
		                   wl_resource_get_version(manager_resource),
		                   id);
	struct tw_seat_client *seat_client =
		tw_seat_client_from_device(pointer_resource);
	struct tw_pointer *pointer = (seat_client) ?
		&seat_client->seat->pointer : NULL;

	if (!resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &swipe_impl, pointer,
	                               handle_gesture_resource_destroy);
	wl_list_insert(manager->swipes.prev, wl_resource_get_link(resource));
}

static void
handle_gestures_get_pinch_gesture(struct wl_client *client,
                                   struct wl_resource *manager_resource,
                                   uint32_t id,
                                  struct wl_resource *pointer_resource)
{
	struct tw_gestures_manager *manager =
		tw_gestures_from_resource(manager_resource);
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &zwp_pointer_gesture_pinch_v1_interface,
		                   wl_resource_get_version(manager_resource),
		                   id);
	struct tw_seat_client *seat_client =
		tw_seat_client_from_device(pointer_resource);
	struct tw_pointer *pointer = (seat_client) ?
		&seat_client->seat->pointer : NULL;

	if (!resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &pinch_impl, pointer,
	                               handle_gesture_resource_destroy);
	wl_list_insert(manager->pinchs.prev, wl_resource_get_link(resource));
}

static void
handle_gestures_release(struct wl_client *client,
                        struct wl_resource *resource)
{
	wl_resource_set_user_data(resource, NULL);
}

static const struct zwp_pointer_gestures_v1_interface gestures_impl = {
	.get_pinch_gesture = handle_gestures_get_pinch_gesture,
	.get_swipe_gesture = handle_gestures_get_swipe_gesture,
	.release = handle_gestures_release,
};

static void
bind_gestures(struct wl_client *client, void *data,
              uint32_t version, uint32_t id)
{
	struct wl_resource *res =
		wl_resource_create(client, &zwp_pointer_gestures_v1_interface,
		                   version, id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &gestures_impl, data,
	                               NULL);
}

static void
tw_gestures_manager_destroy(struct tw_gestures_manager *manager)
{
	tw_reset_wl_list(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
}

static void
notify_gestures_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_gestures_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	tw_gestures_manager_destroy(manager);
}

WL_EXPORT struct tw_gestures_manager *
tw_gestures_manager_create_global(struct wl_display *display)
{
	static struct tw_gestures_manager s_gestures = {0};
	if (!tw_gestures_manager_init(&s_gestures, display))
		return false;
	return &s_gestures;
}

WL_EXPORT bool
tw_gestures_manager_init(struct tw_gestures_manager *manager,
                         struct wl_display *display)
{
	if (!(manager->global =
		    wl_global_create(display,
		                     &zwp_pointer_gestures_v1_interface,
		                     GESTURES_VERSION, manager,
		                     bind_gestures)))
		return false;
	manager->display = display;
	tw_set_display_destroy_listener(display, &manager->display_destroy,
	                                notify_gestures_display_destroy);
	wl_list_init(&manager->pinchs);
	wl_list_init(&manager->swipes);

	return true;
}
