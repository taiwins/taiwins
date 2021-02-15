/*
 * data_device.h - taiwins server wl_data_device implementation
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

#ifndef TW_DATA_DEVICE_H
#define TW_DATA_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>

#include "seat.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct tw_data_device;
struct tw_data_source;
struct tw_data_offer;
struct tw_data_drag;

/**
 * @brief tw_data_offer represents multiple wl_data_offers in the clients.
 *
 * the offer tracks its source, will get notified when source is gone.
 */
struct tw_data_offer {
	struct tw_data_source *source;
	struct wl_resource *current_surface;
	struct wl_listener source_destroy_listener;
	struct wl_list resources;
	bool finished;
};

struct tw_data_source_impl {
	void (*target)(struct tw_data_source *source, const char *mime);
	void (*send)(struct tw_data_source *source, const char *mime, int fd);
	void (*cancel)(struct tw_data_source *source);
	void (*dnd_drop)(struct tw_data_source *source);
	void (*dnd_finish)(struct tw_data_source *source);
	void (*action)(struct tw_data_source *source, uint32_t action);
};

/**
 * @brief tw_data_source represents a wl_data_source for the client
 *
 * the source tracks current offer
 */
struct tw_data_source {
	struct wl_resource *resource;
	/** set at set_selection or start_drag */
	struct wl_array mimes;
	uint32_t actions;
	uint32_t selected_dnd_action;
	bool selection_source;
	bool accepted;
	const struct tw_data_source_impl *impl;

	/**< data offer is embedded in the source available at source
	 * creation */
	struct tw_data_offer offer;

	struct wl_listener device_destroy_listener;
	struct wl_signal destroy_signal;
};

struct tw_data_drag {
	struct tw_data_source *source;
	struct wl_resource *dest_device_resource;
	struct tw_seat_pointer_grab pointer_grab;
	struct tw_seat_keyboard_grab keyboard_grab;
	struct wl_listener source_destroy_listener;
};

/**
 * @brief tw_data_device represents a seat
 *
 * the data_device tracks current data source, will be notified if source is
 * gone.
 */
struct tw_data_device {
	struct wl_list link;
	struct tw_seat *seat;
	struct wl_list clients;
	struct tw_data_source *source_set;
	/**< the drag for this device, since a seat has one drag at a time  */
	struct tw_data_drag drag;

	struct wl_listener source_destroy;
	struct wl_listener create_data_offer;
	struct wl_listener seat_destroy;

	struct wl_signal source_added;
	struct wl_signal source_removed;
};

struct tw_data_device_manager {
	struct wl_global *global;
	struct wl_list devices;

	struct wl_listener display_destroy_listener;
};

struct tw_data_device *
tw_data_device_find(struct tw_data_device_manager *manager,
                    struct tw_seat *seat);
void
tw_data_device_set_selection(struct tw_data_device *device,
                             struct tw_data_source *source);

struct tw_data_device_manager *
tw_data_device_create_global(struct wl_display *display);

bool
tw_data_device_manager_init(struct tw_data_device_manager *manager,
                            struct wl_display *display);
void
tw_data_source_init(struct tw_data_source *source, struct wl_resource *res,
                    const struct tw_data_source_impl *impl);
void
tw_data_source_fini(struct tw_data_source *source);

static inline void
tw_data_source_send_target(struct tw_data_source *source, const char *mime)
{
	if (source->impl->target)
		source->impl->target(source, mime);
}

static inline void
tw_data_source_send_send(struct tw_data_source *source, const char *mime,
                         int fd)
{
	if (source->impl->send)
		source->impl->send(source, mime, fd);
}

static inline void
tw_data_source_send_cancel(struct tw_data_source *source)
{
	if (source->impl->cancel)
		source->impl->cancel(source);
}

static inline void
tw_data_source_send_dnd_drop(struct tw_data_source *source)
{
	if (source->impl->dnd_drop)
		source->impl->dnd_drop(source);
}

static inline void
tw_data_source_send_dnd_finish(struct tw_data_source *source)
{
	if (source->impl->dnd_finish)
		source->impl->dnd_finish(source);
}
static inline void
tw_data_source_send_action(struct tw_data_source *source, uint32_t action)
{
	if (source->impl->action)
		source->impl->action(source, action);
}

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
