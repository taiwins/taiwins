/*
 * region.c - taiwins wl_region implementation
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
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>

#include <taiwins/objects/surface.h>
#include <taiwins/objects/utils.h>

static void
region_add(struct wl_client *client,
           struct wl_resource *resource,
           int32_t x,
           int32_t y,
           int32_t width,
           int32_t height)
{
	struct tw_region *region = tw_region_from_resource(resource);
	pixman_region32_union_rect(&region->region, &region->region,
	                           x, y, width, height);
}

static void
region_subtract(struct wl_client *client,
                struct wl_resource *resource,
                int32_t x,
                int32_t y,
                int32_t width,
                int32_t height)
{
	pixman_region32_t src;
	struct tw_region *region = tw_region_from_resource(resource);

	pixman_region32_init_rect(&src, x, y, width, height);
	pixman_region32_subtract(&region->region, &region->region, &src);
	pixman_region32_fini(&src);
}

static const struct wl_region_interface region_impl = {
	.destroy = tw_resource_destroy_common,
	.add = region_add,
	.subtract = region_subtract,
};

static void
region_destroy_resource(struct wl_resource *resource)
{
	struct tw_region *region = tw_region_from_resource(resource);

	wl_signal_emit(&region->destroy, region);
	pixman_region32_fini(&region->region);
	free(region);
}

WL_EXPORT struct tw_region *
tw_region_from_resource(struct wl_resource *wl_region)
{
	assert(wl_resource_instance_of(wl_region, &wl_region_interface,
	                               &region_impl));
	return wl_resource_get_user_data(wl_region);
}

WL_EXPORT struct tw_region *
tw_region_create(struct wl_client *client, uint32_t ver, uint32_t id,
                 const struct tw_allocator *alloc)
{
	struct wl_resource *resource = NULL;
	struct tw_region *tw_region = NULL;

	if (!tw_alloc_wl_resource_for_obj(resource, tw_region, client, id, ver,
	                                  wl_region_interface, alloc)) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(resource, &region_impl, tw_region,
	                               region_destroy_resource);
	tw_region->resource = resource;
	wl_signal_init(&tw_region->destroy);
	pixman_region32_init(&tw_region->region);
	return tw_region;
}
