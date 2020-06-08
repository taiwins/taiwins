/*
 * buffer.c - taiwins buffer implementation
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
#include <pixman-1/pixman.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#include "ctypes/helpers.h"
#include "surface.h"


void
tw_surface_buffer_update(struct tw_surface_buffer *buffer,
                         struct wl_resource *resource,
                         pixman_region32_t *damage)
{
	struct tw_event_buffer_uploading event;
	//compare if resource is a wl_buffer
	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	int32_t stride = wl_shm_buffer_get_stride(shm_buf);
	int32_t width = wl_shm_buffer_get_width(shm_buf);
	int32_t height = wl_shm_buffer_get_height(shm_buf);
	struct tw_surface *surface =
		container_of(buffer, struct tw_surface, buffer);
	struct tw_surface_manager *manager = surface->manager;
	void *user_data;

	if (buffer->width != width || buffer->height != height ||
	    buffer->stride != stride)
		return;

	if (manager && manager->buffer_import.buffer_import) {
		event.wl_buffer = resource;
		event.damages = damage;
		event.buffer = buffer;
		user_data = manager->buffer_import.callback;
		manager->buffer_import.buffer_import(&event, user_data);
	}
	buffer->resource = resource;
	//maybe I can release the buffer now.
}

void
tw_surface_buffer_new(struct tw_surface_buffer *buffer,
                      struct wl_resource *resource)
{
	struct tw_event_buffer_uploading event = {0};
	struct tw_surface *surface =
		container_of(buffer, struct tw_surface, buffer);
	struct tw_surface_manager *manager = surface->manager;
	void *user_data;

	// trigering the event for uploading buffer to textures, the backend
	// shall take care of differentiating the wl_shm_buffer, dmabuf or
	// wl_drm buffer.
	if (manager && manager->buffer_import.buffer_import) {
		event.wl_buffer = resource;
		event.damages = NULL;
		event.buffer = buffer;
		user_data = manager->buffer_import.callback;
		manager->buffer_import.buffer_import(&event, user_data);
	}
	buffer->resource = resource;
}

void
tw_surface_buffer_release(struct tw_surface_buffer *buffer)
{
	if (!buffer->resource)
		return;
	wl_buffer_send_release(buffer->resource);
	buffer->resource = NULL; //?
}
