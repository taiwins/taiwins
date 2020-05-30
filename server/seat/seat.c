/*
 * seat.c - taiwins server wl_seat implemetation
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

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <ctypes/strops.h>
#include <xkbcommon/xkbcommon.h>

#include "seat.h"

struct tw_seat_client *
tw_seat_client_find(struct tw_seat *seat, struct wl_client *client)
{
	struct tw_seat_client *seat_client = NULL;
	wl_list_for_each(seat_client, &seat->clients, link) {
		if (seat_client->client == client)
			return seat_client;
	}
	return NULL;
}

struct tw_seat_client *
tw_seat_client_new(struct tw_seat *seat, struct wl_client *client,
                   struct wl_resource *resource)
{
	struct tw_seat_client *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->seat = seat;
	s->resource = resource;
	wl_list_init(&s->link);
	wl_list_init(&s->keyboards);
	wl_list_init(&s->pointers);
	wl_list_init(&s->touches);
	wl_list_insert(&seat->clients, &s->link);
	return s;
}

//// listeners
static void
release_device(struct wl_client *client, struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
	wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_impl = {
	.release = release_device,
};

static void
destroy_device(struct wl_resource *wl_resource)
{
	//there is nothing to do.
	wl_list_remove(wl_resource_get_link(wl_resource));
}

static void
seat_client_create_keyboard(struct tw_seat_client *client,
                            uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client->client,
	                              &wl_keyboard_interface,
	                              version, id);
	if (!resource) {
		wl_resource_post_no_memory(client->resource);
		return;
	}
	wl_resource_set_implementation(resource, &keyboard_impl,
	                               client,
	                               destroy_device);
	wl_list_insert(client->keyboards.prev,
	               wl_resource_get_link(resource));

	tw_keyboard_send_keymap(&client->seat->keyboard,
	                        resource);
	wl_keyboard_send_repeat_info(resource, client->seat->repeat_rate,
	                             client->seat->repeat_delay);
}

static void
set_pointer_cursor(struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t serial,
                   struct wl_resource *surface,
                   int32_t hotspot_x,
                   int32_t hotspot_y)
{
	struct tw_seat_client *seat_client =
		wl_resource_get_user_data(resource);
	struct tw_seat *seat = seat_client->seat;

	struct tw_pointer_new_cursor_event new_cursor_event = {
		.surface = surface,
		.hotspot_x = hotspot_x,
		.hotspot_y = hotspot_y,
	};

	wl_signal_emit(&seat->new_cursor_signal, &new_cursor_event);
}

static const struct wl_pointer_interface pointer_impl = {
	.set_cursor = set_pointer_cursor,
	.release = release_device,
};

static void
seat_client_create_pointer(struct tw_seat_client *client,
                           uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client->client,
	                              &wl_pointer_interface,
	                              version, id);
	if (!resource) {
		wl_resource_post_no_memory(client->resource);
		return;
	}
	wl_resource_set_implementation(resource, &pointer_impl,
	                               client,
	                               destroy_device);
	wl_list_insert(client->pointers.prev,
	               wl_resource_get_link(resource));
}

static const struct wl_touch_interface touch_impl = {
	.release = release_device,
};

static void
seat_client_create_touch(struct tw_seat_client *client,
                         uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client->client,
	                              &wl_touch_interface,
	                              version, id);
	if (!resource) {
		wl_resource_post_no_memory(client->resource);
		return;
	}
	wl_resource_set_implementation(resource, &touch_impl,
	                               client,
	                               destroy_device);
	wl_list_insert(client->touches.prev,
	               wl_resource_get_link(resource));
}

static void
seat_get_keyboard(struct wl_client *client,
                  struct wl_resource *seat_resource, uint32_t id)
{
	struct tw_seat_client *seat_client =
		wl_resource_get_user_data(seat_resource);
	uint32_t version = wl_resource_get_version(seat_resource);

	if (seat_client->seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		seat_client_create_keyboard(seat_client, version, id);
	else
		wl_resource_post_error(seat_resource, 1,
		                       "seat %u does not have a keyboard",
		                       id);
}

static void
seat_get_pointer(struct wl_client *client, struct wl_resource *seat_resource,
                 uint32_t id)
{
	struct tw_seat_client *seat_client =
		wl_resource_get_user_data(seat_resource);
	uint32_t version = wl_resource_get_version(seat_resource);

	if (seat_client->seat->capabilities & WL_SEAT_CAPABILITY_POINTER)
		seat_client_create_pointer(seat_client, version, id);
	else
		wl_resource_post_error(seat_resource, 1,
		                       "seat %u does not have a keyboard",
		                       id);
}

static void
seat_get_touch(struct wl_client *client, struct wl_resource *seat_resource,
               uint32_t id)
{
	struct tw_seat_client *seat_client =
		wl_resource_get_user_data(seat_resource);
	uint32_t version = wl_resource_get_version(seat_resource);

	if (seat_client->seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)
		seat_client_create_touch(seat_client, version, id);
	else
		wl_resource_post_error(seat_resource, 1,
		                       "seat %u does not have a touch",
		                       id);
}

static void
seat_client_release(struct wl_client *client,
                    struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_impl = {
	.get_pointer = seat_get_keyboard,
	.get_keyboard = seat_get_pointer,
	.get_touch = seat_get_touch,
	.release = seat_client_release,
};

static void
destroy_seat_client(struct wl_resource *seat_resource)
{
	struct wl_resource *resource, *tmp;
	struct tw_seat_client *sc =
		wl_resource_get_user_data(seat_resource);

	wl_list_remove(&sc->link);
	wl_resource_for_each_safe(resource, tmp, &sc->keyboards)
		wl_resource_destroy(resource);
	wl_resource_for_each_safe(resource, tmp, &sc->pointers)
		wl_resource_destroy(resource);
	wl_resource_for_each_safe(resource, tmp, &sc->touches)
		wl_resource_destroy(resource);
	free(sc);
}

static void
bind_seat(struct wl_client *client, void *data,
          uint32_t version, uint32_t id)
{
	struct tw_seat_client *seat_client;
	struct tw_seat *seat = data;
	struct wl_resource *wl_resource =
		wl_resource_create(client, &wl_seat_interface, version, id);
	if (!wl_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	seat_client = tw_seat_client_find(seat, client);
	if (!seat_client) {
		seat_client = tw_seat_client_new(seat, client,
		                                 wl_resource);
		if (seat_client) {
			wl_resource_post_no_memory(wl_resource);
			return;
		}
	}
	wl_resource_set_implementation(wl_resource, &seat_impl, seat_client,
	                               destroy_seat_client);

	wl_seat_send_name(wl_resource, seat->name);
	wl_seat_send_capabilities(wl_resource, seat->capabilities);
}

///// shared API

struct tw_seat *
tw_seat_create(struct wl_display *display, const char *name)
{
	struct tw_seat *seat = calloc(1, sizeof(struct tw_seat));
	if (!seat)
		return NULL;

	strop_ncpy(seat->name, name, 32);
	wl_list_init(&seat->clients);
	seat->capabilities = 0;
	seat->repeat_delay = 500;
	seat->repeat_rate = 25;
	seat->display = display;

	wl_signal_init(&seat->new_cursor_signal);
	seat->global = wl_global_create(display, &wl_seat_interface, 7,
	                                seat, bind_seat);
	return seat;
}

void
tw_seat_destroy(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	wl_global_destroy(seat->global);

	wl_list_for_each(client, &seat->clients, link)
		wl_resource_destroy(client->resource);
	free(seat);
}

void
tw_seat_set_name(struct tw_seat *seat, const char *name)
{
	struct tw_seat_client *client, *next;

	strop_ncpy(seat->name, name, 32);
	wl_list_for_each_safe(client, next, &seat->clients, link) {
		wl_seat_send_name(client->resource, seat->name);
	}
}

void
tw_seat_set_key_repeat_rate(struct tw_seat *seat, uint32_t delay,
                            uint32_t rate)
{
	struct tw_seat_client *client;
	struct wl_resource *keyboard;

	seat->repeat_rate = rate;
	seat->repeat_delay = delay;

	wl_list_for_each(client, &seat->clients, link) {
		wl_resource_for_each(keyboard, &client->keyboards)
			wl_keyboard_send_repeat_info(keyboard, rate, delay);
	}
}

void
tw_seat_send_capabilities(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	wl_list_for_each(client, &seat->clients, link)
		wl_seat_send_capabilities(client->resource,
		                          seat->capabilities);
}
