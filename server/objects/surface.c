/*
 * surface.c - taiwins wl_surface implementation
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
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>
#include <wayland-util.h>

#include "ctypes/helpers.h"
#include "surface.h"

#define CALLBACK_VERSION 1
#define SURFACE_VERSION 4
#define SUBSURFACE_VERSION 1

/******************************************************************************
 * wl_surface implementation
 *****************************************************************************/

static void
surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
surface_attach(struct wl_client *client,
               struct wl_resource *resource,
               struct wl_resource *buffer,
               int32_t x,
               int32_t y)
{
	struct tw_surface *surface = tw_surface_from_resource(resource);
	surface->pending->dx = x;
	surface->pending->dy = y;
	surface->pending->buffer_resource = buffer;
}

static void
surface_damage(struct wl_client *client,
               struct wl_resource *resource,
               int32_t x,
               int32_t y,
               int32_t width,
               int32_t height)
{
	struct tw_surface *surface = tw_surface_from_resource(resource);
	if (width < 0 || height < 0)
		return;
	pixman_region32_union_rect(&surface->pending->surface_damage,
	                           &surface->pending->surface_damage,
	                           x, y, width, height);
}

static void
callback_destroy_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
surface_frame(struct wl_client *client,
              struct wl_resource *resource,
              uint32_t callback)
{
	struct tw_surface *surface = tw_surface_from_resource(resource);
	struct wl_resource *res_callback =
		wl_resource_create(client, &wl_callback_interface,
		                   CALLBACK_VERSION, callback);
	if (!res_callback) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(res_callback, NULL, NULL,
	                               callback_destroy_resource);
	wl_list_insert(&surface->frame_callbacks,
	               wl_resource_get_link(res_callback));
	//change state.
}

static void
surface_set_opaque_region(struct wl_client *client,
                          struct wl_resource *res,
                          struct wl_resource *region_res)
{
	struct tw_region *region;
	struct tw_surface *surface = tw_surface_from_resource(res);
	if (!region_res) {
		pixman_region32_clear(&surface->pending->opaque_region);
	} else {
		region = tw_region_from_resource(region_res);
		pixman_region32_union(&surface->pending->opaque_region,
		                      &surface->pending->opaque_region,
		                      &region->region);
	}
}

static void
surface_set_input_region(struct wl_client *client,
                         struct wl_resource *res,
                         struct wl_resource *region_res)
{
	struct tw_region *region;
	struct tw_surface *surface = tw_surface_from_resource(res);
	if (!region_res) {
		pixman_region32_fini(&surface->pending->input_region);
		pixman_region32_init_rect(&surface->pending->input_region,
			INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	} else {
		region = tw_region_from_resource(region_res);
		pixman_region32_copy(&surface->pending->input_region,
		                     &region->region);
	}
}

static void
copy_state(struct tw_view *dst, struct tw_view *src)
{
	dst->transform = src->transform;
	dst->buffer_scale = src->buffer_scale;

	pixman_region32_copy(&dst->input_region, &src->input_region);
	pixman_region32_copy(&dst->opaque_region, &src->opaque_region);
}

static void
update_surface_buffer(struct tw_surface *surface)
{
	struct wl_resource *resource = surface->current->buffer_resource;
	pixman_region32_t *damage = &surface->current->buffer_damage;
	//so far here is only place we release the buffer, this require users to
	//have double-buffered surface. Maybe we can early release the buffer.
	if (surface->previous->buffer_resource) {
		assert(surface->buffer.resource ==
		       surface->previous->buffer_resource);
		tw_surface_buffer_release(&surface->buffer);
		surface->previous->buffer_resource = NULL;
	}
	//if there is no buffer for us, we can leave
	if (!resource)
		return;
	//now we upload the buffer, buffer uploading requires updating the
	//textures, if we already have the texture, updating it would be enough.
	if (tw_surface_has_texture(surface))
		tw_surface_buffer_update(&surface->buffer, resource,
		                         damage);
	else
		tw_surface_buffer_new(&surface->buffer, resource);
}

static void
surface_commit_state(struct tw_surface *surface)
{
	struct tw_view *committed = surface->current;
	struct tw_view *pending = surface->pending;
	struct tw_view *previous = surface->previous;

        surface->current = pending;
	surface->previous = committed;
	surface->pending = previous;

	//clear pading state
	pixman_region32_clear(&surface->pending->surface_damage);
	pixman_region32_clear(&surface->pending->buffer_damage);
	//convert surface damage to buffer damage.
	pixman_region32_translate(&surface->current->surface_damage,
	                          surface->current->dx,
	                          surface->current->dy);
	pixman_region32_union(&surface->current->buffer_damage,
	                      &surface->current->buffer_damage,
	                      &surface->current->surface_damage);
	//copy state
	copy_state(surface->pending, surface->current);
	update_surface_buffer(surface);
	//TODO if the buffer updates is finished, maybe we can actually release
	//it now
	//TODO now we need to apply the subsurfaces

	//also commit the subsurface surface and
	if (surface->role.commit)
		surface->role.commit(surface);
}

/* synched if one of the surface superiers is synched */
static bool
subsurface_synched(struct tw_subsurface *subsurface)
{
	while (subsurface != NULL) {
		if (subsurface->sync)
			return true;
		if (!subsurface->parent)
			return false;
		subsurface = subsurface->parent->subsurface;
	}

	return false;
}

static void
subsurface_commit_parent(struct tw_subsurface *subsurface, bool sync)
{
	//I feel like the logic here is not right at all. But I don't know,
	//handling subsurfaces is, in any case, we do not have test example
	//here, so I cannot be sure.
	struct tw_subsurface *child;
	struct tw_surface *surface = subsurface->surface;
	if (sync || subsurface->sync) {
		//we do not have a cache state, maybe required though
		surface_commit_state(surface);
		wl_list_for_each(child, &surface->subsurfaces, parent_link)
			subsurface_commit_parent(child, true);
	}
}

static inline void
subsurface_commit(struct tw_subsurface *subsurface, bool forced)
{
	//if subsurface is sync state, then we do not apply the commit
	//immediately.
	if (forced || !subsurface_synched(subsurface))
		surface_commit_state(subsurface->surface);
}

static void
surface_commit(struct wl_client *client,
              struct wl_resource *resource)
{
	struct tw_subsurface *subsurface;
	struct tw_surface *surface = tw_surface_from_resource(resource);

	//either to commit now or later when the parent commits.
	if (surface->subsurface)
		subsurface_commit(surface->subsurface, false);
	else
		surface_commit_state(surface);
	//also commit all the children
	wl_list_for_each(subsurface, &surface->subsurfaces, parent_link)
		subsurface_commit_parent(subsurface, false);
	//if we have manager, we can generate events now for additional
	//processing,
	wl_signal_emit(&surface->events.commit, surface);
}

static void
surface_set_buffer_transform(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t transform)
{
	struct tw_surface *surface;

	if (transform < WL_OUTPUT_TRANSFORM_NORMAL ||
	    transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
		wl_resource_post_error(resource,
		                       WL_SURFACE_ERROR_INVALID_TRANSFORM,
		                       "surface transform value %d is invalid",
		                       transform);
		return;
	}
	surface = tw_surface_from_resource(resource);
	surface->pending->transform = transform;
}

static void
surface_set_buffer_scale(struct wl_client *client,
                         struct wl_resource *resource, int32_t scale)
{
	struct tw_surface *surface;
	if (scale < 0) {
		wl_resource_post_error(resource,
		                       WL_SURFACE_ERROR_INVALID_SCALE,
		                       "surface buffer scale %d is invalid",
		                       scale);
		return;
	}
	surface = tw_surface_from_resource(resource);
	surface->pending->buffer_scale = scale;
}

static void
surface_damage_buffer(struct wl_client *client,
                      struct wl_resource *resource,
                      int32_t x, int32_t y, int32_t width,
                      int32_t height)
{
	struct tw_surface *surface = tw_surface_from_resource(resource);
	if (width < 0 || height < 0)
		return;
	pixman_region32_union_rect(&surface->pending->buffer_damage,
	                           &surface->pending->buffer_damage,
	                           x, y, width, height);
}

static const struct wl_surface_interface surface_impl = {
	.destroy = surface_destroy,
	.attach = surface_attach,
	.frame = surface_frame,
	.damage = surface_damage,
	.set_opaque_region = surface_set_opaque_region,
	.set_input_region = surface_set_input_region,
	.commit = surface_commit,
	.set_buffer_transform = surface_set_buffer_transform,
	.set_buffer_scale = surface_set_buffer_scale,
	.damage_buffer = surface_damage_buffer,
};

static void
surface_destroy_resource(struct wl_resource *resource)
{
	struct tw_view *view;
	struct tw_surface *surface = tw_surface_from_resource(resource);
	for (int i = 0; i < 3; i++) {
		view = &surface->surface_states[i];
		pixman_region32_fini(&view->surface_damage);
		pixman_region32_fini(&view->buffer_damage);
		pixman_region32_fini(&view->input_region);
		pixman_region32_fini(&view->opaque_region);
	}

#ifdef TW_OVERLAY_PLANE
	for (int i = 0; i < 32; i++)
		pixman_region32_fini(&surface->output_damages[i]);
#endif
	if (surface->buffer.resource)
		tw_surface_buffer_release(&surface->buffer);

	wl_signal_emit(&surface->events.destroy, surface);

	free(surface);
}

struct tw_surface *
tw_surface_from_resource(struct wl_resource *wl_surface)
{
	assert(wl_resource_instance_of(wl_surface,
	                               &wl_surface_interface,
	                               &surface_impl));
	return wl_resource_get_user_data(wl_surface);
}

bool
tw_surface_has_texture(struct tw_surface *surface)
{
	return surface->buffer.handle.ptr || surface->buffer.handle.id;
}

struct tw_surface *
tw_surface_create(struct wl_client *client, uint32_t version, uint32_t id,
                  struct tw_surface_manager *manager)
{
	struct tw_view *view;
	struct wl_resource *resource;
	struct tw_surface *surface = calloc(1, sizeof(struct tw_surface));
	if (!surface) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	resource = wl_resource_create(client, &wl_surface_interface,
	                              version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		free(surface);
		return NULL;
	}
	wl_resource_set_implementation(resource, &surface_impl, surface,
	                               surface_destroy_resource);
	//initializers
	surface->manager = manager;
	surface->resource = resource;
	surface->state = 0;
	surface->is_mapped = false;
	surface->subsurface = NULL;
	surface->pending = &surface->surface_states[0];
	surface->current = &surface->surface_states[1];
	surface->previous = &surface->surface_states[2];
	wl_signal_init(&surface->events.commit);
	wl_signal_init(&surface->events.frame);
	wl_signal_init(&surface->events.destroy);

	for (int i = 0; i < 3; i++) {
		view = &surface->surface_states[i];
		view->transform = WL_OUTPUT_TRANSFORM_NORMAL;
		view->buffer_scale = 1;
		pixman_region32_init(&view->surface_damage);
		pixman_region32_init(&view->buffer_damage);
		pixman_region32_init(&view->opaque_region);
		pixman_region32_init(&view->input_region);
	}

#ifdef TW_OVERLAY_PLANE
	for (int i = 0; i < 32; i++)
		pixman_region32_init(&surface->output_damages[i]);
#endif

	wl_list_init(&surface->subsurfaces);
	wl_list_init(&surface->frame_callbacks);
	if (manager)
		wl_signal_emit(&manager->surface_created_signal, surface);
	return surface;
}

/******************************************************************************
 * wl_subsurface implementation
 *****************************************************************************/
static const struct wl_subsurface_interface subsurface_impl;

static struct tw_subsurface *
tw_subsurface_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wl_subsurface_interface,
	                               &subsurface_impl));
	return wl_resource_get_user_data(resource);
}

static struct tw_subsurface *
find_sibling_subsurface(struct tw_subsurface *subsurface,
                        struct tw_surface *surface)
{
	struct tw_surface *parent = subsurface->parent;
	struct tw_subsurface *sibling;
	wl_list_for_each(sibling, &parent->subsurfaces, parent_link) {
		if (sibling->surface == surface && sibling != subsurface)
			return sibling;
	}
	return NULL;
}

static void
subsurface_handle_destroy(struct wl_client *client,
                          struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
subsurface_set_position(struct wl_client *client,
                        struct wl_resource *resource,
                        int32_t x,
                        int32_t y)
{
	struct tw_subsurface *subsurf =
		tw_subsurface_from_resource(resource);
	subsurf->sx = x;
	subsurf->sy = y;
}

static void
subsurface_place_above(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *sibling)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	struct tw_surface *sibling_surface =
		tw_surface_from_resource(sibling);
	struct tw_subsurface *sibling_subsurface =
		find_sibling_subsurface(subsurface, sibling_surface);
	if (!sibling_subsurface) {
		wl_resource_post_error(
			resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"wl_surface@%d is not sibling to "
			"wl_surface@%d",
			wl_resource_get_id(sibling_surface->resource),
			wl_resource_get_id(subsurface->surface->resource));
	}
	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_insert(&sibling_subsurface->parent_pending_link,
	               &subsurface->parent_pending_link);
}

static void
subsurface_place_below(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *sibling)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	struct tw_surface *sibling_surface =
		tw_surface_from_resource(sibling);
	struct tw_subsurface *sibling_subsurface =
		find_sibling_subsurface(subsurface, sibling_surface);
	if (!sibling_subsurface) {
		wl_resource_post_error(
			resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"wl_surface@%d is not sibling to "
			"wl_surface@%d",
			wl_resource_get_id(sibling_surface->resource),
			wl_resource_get_id(subsurface->surface->resource));
	}
	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_insert(sibling_subsurface->parent_pending_link.prev,
	               &subsurface->parent_pending_link);

}

static void
subsurface_set_sync(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	subsurface->sync = true;
}

static void
subsurface_set_desync(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	if (subsurface->sync) {
		subsurface->sync = false;
		//TODO try to commit pending parents
	}
}

static const struct wl_subsurface_interface subsurface_impl = {
	.destroy = subsurface_handle_destroy,
	.set_position = subsurface_set_position,
	.place_above = subsurface_place_above,
	.place_below = subsurface_place_below,
	.set_sync = subsurface_set_sync,
	.set_desync = subsurface_set_desync,
};

static void
subsurface_destroy(struct tw_subsurface *subsurface)
{
	//TODO there are more work
	if (!subsurface)
		return;
	wl_list_remove(&subsurface->surface_destroyed.link);
	if (subsurface->parent) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_remove(&subsurface->parent_pending_link);
	}
	wl_resource_set_user_data(subsurface->resource, NULL);
	if (subsurface->surface)
		subsurface->surface->subsurface = NULL;
	subsurface->parent = NULL;
	free(subsurface);
}

static void
subsurface_destroy_resource(struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	subsurface_destroy(subsurface);
}

static void
notify_subsurface_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_subsurface *subsurface =
		container_of(listener, struct tw_subsurface,
		             surface_destroyed);
	subsurface_destroy(subsurface);
}

struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t version,
                     uint32_t id, struct tw_surface *surface,
                     struct tw_surface *parent)
{
	struct tw_subsurface *subsurface =
		calloc(1, sizeof(struct tw_subsurface));
	if (!subsurface) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	struct wl_resource *resource =
		wl_resource_create(client, &wl_subsurface_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		free(subsurface);
		return NULL;
	}
	wl_resource_set_implementation(resource, &wl_subsurface_interface,
	                               subsurface,
	                               subsurface_destroy_resource);
	surface->subsurface = subsurface;
	subsurface->resource = resource;
	subsurface->surface = surface;
	subsurface->parent = parent;
	//add listeners
	wl_list_init(&subsurface->surface_destroyed.link);
	subsurface->surface_destroyed.notify =
		notify_subsurface_surface_destroy;
	wl_signal_add(&surface->events.destroy,
	              &subsurface->surface_destroyed);

	if (surface->manager)
		wl_signal_emit(&surface->manager->subsurface_created_signal,
		               subsurface);

	return subsurface;
}

void
tw_surface_manager_init(struct tw_surface_manager *manager)
{
	wl_signal_init(&manager->surface_created_signal);
	wl_signal_init(&manager->subsurface_created_signal);
	wl_signal_init(&manager->region_created_signal);
}
