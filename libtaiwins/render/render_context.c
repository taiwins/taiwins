/*
 * render_context.c - taiwins render context
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
#include "options.h"

#include <assert.h>
#include <stdlib.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>
#include <wayland-server.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/render_surface.h>
#include <taiwins/objects/logger.h>
#include <taiwins/profiling.h>

/******************************************************************************
 * tw_render_surface APIs
 *****************************************************************************/

static void
notify_tw_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.destroy);
	assert(data == &surface->surface);
	wl_signal_emit(&surface->ctx->events.wl_surface_destroy, data);
	tw_render_surface_fini(surface);
}

static void
notify_tw_surface_dirty(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.dirty);
	assert(data == &surface->surface);
	//forwarding the dirty event.
	wl_signal_emit(&surface->ctx->events.wl_surface_dirty, data);
}

static void
notify_tw_surface_output_lost(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.output_lost);
	tw_render_surface_reassign_outputs(surface, surface->ctx);
}

void
tw_render_surface_init(struct tw_render_surface *surface,
                       struct tw_render_context *ctx)
{
	struct tw_surface *tw_surface = &surface->surface;

	wl_list_init(&surface->listeners.commit.link);
	wl_list_init(&surface->listeners.destroy.link);
	wl_list_init(&surface->listeners.dirty.link);
	wl_list_init(&surface->listeners.frame.link);
	wl_list_init(&surface->listeners.output_lost.link);

	pixman_region32_init(&surface->clip);
	surface->ctx = ctx;
#ifdef TW_OVERLAY_PLANE
	for (int i = 0; i < 32; i++)
		pixman_region32_init(&surface->output_damage[i]);
#endif
	tw_signal_setup_listener(&tw_surface->events.destroy,
	                         &surface->listeners.destroy,
	                         notify_tw_surface_destroy);
	tw_signal_setup_listener(&tw_surface->events.dirty,
	                         &surface->listeners.dirty,
	                         notify_tw_surface_dirty);
	tw_signal_setup_listener(&ctx->events.output_lost,
	                         &surface->listeners.output_lost,
	                         notify_tw_surface_output_lost);
}

void
tw_render_surface_fini(struct tw_render_surface *surface)
{
	pixman_region32_fini(&surface->clip);
#ifdef TW_OVERLAY_PLANE
	for (int i = 0; i < 32; i++)
		pixman_region32_fini(&surface->output_damage[i]);
#endif
	wl_list_remove(&surface->listeners.destroy.link);
	wl_list_remove(&surface->listeners.dirty.link);
	wl_list_remove(&surface->listeners.frame.link);
	wl_list_remove(&surface->listeners.commit.link);
}

struct tw_render_surface *
tw_render_surface_from_resource(struct wl_resource *resource)
{
	struct tw_surface *tw_surface = (resource) ?
		tw_surface_from_resource(resource) : NULL;
	struct tw_render_surface *surface = (tw_surface) ?
		wl_container_of(tw_surface, surface, surface) : NULL;
	return surface;
}


/******************************************************************************
 * render_context allocator
 *****************************************************************************/

static const struct tw_allocator tw_render_compositor_allocator;

static void *
handle_alloc_compositor_obj(size_t size, const struct wl_interface *interface)
{
	if (interface == &wl_surface_interface) {
		struct tw_render_surface *surface = NULL;

		assert(size == sizeof(struct tw_surface));
		assert(interface == &wl_surface_interface);

		surface = calloc(1, sizeof(*surface));
		return &surface->surface;
	} else if (interface == &wl_subsurface_interface) {
		assert(size == sizeof(struct tw_subsurface));
		return calloc(1, size);
	} else if (interface == &wl_region_interface) {
		assert(size == sizeof(struct tw_region));
		return calloc(1, size);
	} else {
		tw_logl_level(TW_LOG_ERRO, "invalid interface");
		assert(0);
	}
}

static void
handle_free_compositor_obj(void *ptr, const struct wl_interface *interface)
{
	if (interface == &wl_surface_interface) {
		struct tw_surface *tw_surface = ptr;
		struct tw_render_surface *surface =
			wl_container_of(ptr, surface, surface);
		assert(interface == &wl_surface_interface);
		assert(tw_surface->alloc == &tw_render_compositor_allocator);
		free(surface);
	} else if (interface == &wl_subsurface_interface) {
		struct tw_subsurface *subsurface = ptr;
		assert(subsurface->alloc == &tw_render_compositor_allocator);
		free(ptr);
	} else if (interface == &wl_region_interface) {
		struct tw_region *region = ptr;
		assert(region->alloc == &tw_render_compositor_allocator);
		free(ptr);
	} else {
		tw_logl_level(TW_LOG_ERRO, "invalid interface");
		assert(0);
	}
}

static const struct tw_allocator tw_render_compositor_allocator =  {
	.alloc = handle_alloc_compositor_obj,
	.free = handle_free_compositor_obj,
};

/******************************************************************************
 * tw_render_context APIs
 *****************************************************************************/

/** I think this will simply seat here even if we have a vulkan render context,
 EGL render context will be compulsory */
void
tw_render_context_destroy(struct tw_render_context *ctx)
{
	ctx->display_destroy.notify(&ctx->display_destroy, ctx->display);
}

static void
subsurface_add_to_list(struct wl_list *parent_head, struct tw_surface *surface)
{
	struct tw_subsurface *sub;

	wl_list_insert(parent_head->prev,
	               &surface->links[TW_VIEW_GLOBAL_LINK]);
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link) {
		subsurface_add_to_list(&surface->links[TW_VIEW_GLOBAL_LINK],
		                       sub->surface);
	}
}

static void
surface_add_to_list(struct tw_layers_manager *manager,
                    struct tw_surface *surface)
{
	//we should also add to the output
	struct tw_subsurface *sub;

	wl_list_insert(manager->views.prev,
	               &surface->links[TW_VIEW_GLOBAL_LINK]);

	//subsurface inserts just above its main surface, here we take the
	//reverse order of the subsurfaces and insert them one by one in front
	//of the main surface
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link)
		subsurface_add_to_list(&surface->links[TW_VIEW_GLOBAL_LINK],
		                       sub->surface);
}

static void
surface_add_to_outputs_list(struct tw_render_context *ctx,
                            struct tw_surface *surface)
{
	struct tw_subsurface *sub;
	struct tw_render_output *tmp, *output = NULL;
	struct tw_render_surface *render_surface =
		wl_container_of(surface, render_surface, surface);

        wl_list_for_each(tmp, &ctx->outputs, link) {
		if (tmp->device.id == render_surface->output) {
			output = tmp;
			break;
		}
	}
	assert(output);
	assert(render_surface->output >= 0 && render_surface->output <= 31);

	wl_list_insert(output->views.prev,
	               &surface->links[TW_VIEW_OUTPUT_LINK]);

	wl_list_for_each(sub, &surface->subsurfaces, parent_link)
		surface_add_to_outputs_list(ctx, sub->surface);
}

void
tw_render_context_build_view_list(struct tw_render_context *ctx,
                                  struct tw_layers_manager *manager)
{
	struct tw_surface *surface;
	struct tw_layer *layer;
	struct tw_render_output *output;

	SCOPE_PROFILE_BEG();

	wl_list_init(&manager->views);
	wl_list_for_each(output, &ctx->outputs, link)
		wl_list_init(&output->views);

	wl_list_for_each(layer, &manager->layers, link) {
		wl_list_for_each(surface, &layer->views, layer_link) {
			surface_add_to_list(manager, surface);
			surface_add_to_outputs_list(ctx, surface);
		}
	}

	SCOPE_PROFILE_END();
}

static void
update_surface_mask(struct tw_surface *tw_surface,
                    struct tw_render_output *major, uint32_t mask)
{
	struct tw_render_output *output;
	struct tw_render_surface *surface =
		wl_container_of(tw_surface, surface, surface);
	uint32_t output_bit;
	uint32_t different = surface->output_mask ^ mask;
	uint32_t entered = mask & different;
	uint32_t left = surface->output_mask & different;

	assert(major->ctx);

	//update the surface_mask and
	surface->output_mask = mask;
	surface->output = major->device.id;

	wl_list_for_each(output, &major->ctx->outputs, link) {
		output_bit = 1u << output->device.id;
		if (!(output_bit & different))
			continue;
		if ((output_bit & entered))
			wl_signal_emit(&output->events.surface_enter,
			               tw_surface);
		if ((output_bit & left))
			wl_signal_emit(&output->events.surface_leave,
			               tw_surface);
	}
}

void
tw_render_surface_reassign_outputs(struct tw_render_surface *render_surface,
                                   struct tw_render_context *ctx)
{
	uint32_t area = 0, max = 0, mask = 0;
	struct tw_render_output *output, *major = NULL;
	pixman_region32_t surface_region;
	pixman_box32_t *e;
	struct tw_surface *surface = &render_surface->surface;

	pixman_region32_init_rect(&surface_region,
	                          surface->geometry.xywh.x,
	                          surface->geometry.xywh.y,
	                          surface->geometry.xywh.width,
	                          surface->geometry.xywh.height);
	wl_list_for_each(output, &ctx->outputs, link) {
		pixman_region32_t clip;
		struct tw_output_device *device = &output->device;
		pixman_rectangle32_t rect =
			tw_output_device_geometry(device);
		//TODO dealing with cloning output
		// if (output->cloning >= 0)
		//	continue;
		pixman_region32_init_rect(&clip, rect.x, rect.y,
		                          rect.width, rect.height);
		pixman_region32_intersect(&clip, &clip, &surface_region);
		e = pixman_region32_extents(&clip);
		area = (e->x2 - e->x1) * (e->y2 - e->y1);
		if (pixman_region32_not_empty(&clip))
			mask |= (1u << device->id);
		if (area >= max) {
			major = output;
			max = area;
		}
		pixman_region32_fini(&clip);
	}
	pixman_region32_fini(&surface_region);

	update_surface_mask(surface, major, mask);
}

void
tw_render_context_set_compositor(struct tw_render_context *ctx,
                                 struct tw_compositor *compositor)
{
	wl_signal_emit(&ctx->events.compositor_set, compositor);
	compositor->obj_alloc = &tw_render_compositor_allocator;
}

void
tw_render_context_set_dma(struct tw_render_context *ctx,
                          struct tw_linux_dmabuf *dma)
{
	wl_signal_emit(&ctx->events.dma_set, dma);
}
