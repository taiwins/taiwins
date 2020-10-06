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
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>


WL_EXPORT bool
tw_surface_buffer_update(struct tw_surface_buffer *buffer,
                         struct wl_resource *resource,
                         pixman_region32_t *damage)
{
	struct tw_event_buffer_uploading event;
	//compare if resource is a wl_buffer
	struct tw_surface *surface = wl_container_of(buffer, surface, buffer);
	void *user_data;
	bool ret = false;

	if (buffer->buffer_import.buffer_import) {
		event.wl_buffer = resource;
		event.damages = damage;
		event.buffer = buffer;
		event.new_upload = false;
		user_data = buffer->buffer_import.callback;
		ret = buffer->buffer_import.buffer_import(&event, user_data);
	}
	//if updating failed, nothing changes.
	if (ret)
		buffer->resource = resource;
	return ret;
}

WL_EXPORT void
tw_surface_buffer_new(struct tw_surface_buffer *buffer,
                      struct wl_resource *resource)
{
	struct tw_event_buffer_uploading event = {0};
	struct tw_surface *surface = wl_container_of(buffer, surface, buffer);
	void *user_data;

	if (buffer->buffer_import.buffer_import) {
		event.new_upload = true;
		event.wl_buffer = resource;
		event.damages = NULL;
		event.buffer = buffer;
		user_data = buffer->buffer_import.callback;
		buffer->buffer_import.buffer_import(&event, user_data);
	}
	if (tw_surface_has_texture(surface))
		buffer->resource = resource;
}

WL_EXPORT void
tw_surface_buffer_release(struct tw_surface_buffer *buffer)
{
	if (!buffer->resource)
		return;
	wl_buffer_send_release(buffer->resource);
	buffer->resource = NULL; //?
}
