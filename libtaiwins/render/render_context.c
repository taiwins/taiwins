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
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/render_surface.h>
#include <taiwins/objects/subsurface.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>

#include "utils.h"

/******************************************************************************
 * tw_render_surface APIs
 *****************************************************************************/

static void
notify_tw_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.destroy);
	assert(data == &surface->surface);
	wl_signal_emit(&surface->ctx->signals.wl_surface_destroy, data);
	tw_render_surface_fini(surface);
}

static void
notify_tw_surface_dirty(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.dirty);
	assert(data == &surface->surface);
	//forwarding the dirty event.
	wl_signal_emit(&surface->ctx->signals.wl_surface_dirty, data);
}

static void
notify_tw_surface_output_lost(struct wl_listener *listener, void *data)
{
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.output_lost);
	tw_surface_dirty_geometry(&surface->surface);
}

static void
notify_tw_surface_frame_request(struct wl_listener *listener, void *data)
{
	//wl_surface requested a frame but no buffer committed, we have to run
	//through a frame here
	struct tw_render_surface *surface =
		wl_container_of(listener, surface, listeners.frame);
	assert(data == &surface->surface);
	wl_signal_emit(&surface->ctx->signals.wl_surface_dirty, data);
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
	tw_signal_setup_listener(&tw_surface->signals.destroy,
	                         &surface->listeners.destroy,
	                         notify_tw_surface_destroy);
	tw_signal_setup_listener(&tw_surface->signals.dirty,
	                         &surface->listeners.dirty,
	                         notify_tw_surface_dirty);
	tw_signal_setup_listener(&tw_surface->signals.frame,
	                         &surface->listeners.frame,
	                         notify_tw_surface_frame_request);
	tw_signal_setup_listener(&ctx->signals.output_lost,
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
	wl_list_remove(&surface->listeners.output_lost.link);
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
		return NULL;
	}
}

static void
handle_free_compositor_obj(void *ptr, const struct wl_interface *interface)
{
	if (interface == &wl_surface_interface) {
		struct tw_surface *tw_surface = ptr;
		struct tw_render_surface *surface =
			wl_container_of(tw_surface, surface, surface);
		assert(interface == &wl_surface_interface);
		assert(tw_surface->alloc == &tw_render_compositor_allocator);
		free(surface);
	} else if (interface == &wl_subsurface_interface) {
		struct tw_subsurface *subsurface = ptr;
		assert(subsurface->alloc == &tw_render_compositor_allocator);
		free(subsurface);
	} else if (interface == &wl_region_interface) {
		struct tw_region *region = ptr;
		assert(region->alloc == &tw_render_compositor_allocator);
		free(region);
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
WL_EXPORT void
tw_render_context_destroy(struct tw_render_context *ctx)
{
	ctx->display_destroy.notify(&ctx->display_destroy, ctx->display);
}

static void
subsurface_add_to_list(struct wl_list *parent, struct tw_surface *surface,
                       enum tw_subsurface_pos pos)
{
	struct tw_subsurface *sub;
	//insert subsurface BEFORE the parent if it is placed above or AFTER
	//the if it is placed below
	struct wl_list *node =
		(pos == TW_SUBSURFACE_ABOVE) ? parent->prev : parent;

	wl_list_insert(node, &surface->links[TW_VIEW_GLOBAL_LINK]);
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link) {
		subsurface_add_to_list(&surface->links[TW_VIEW_GLOBAL_LINK],
		                       sub->surface, sub->pos);
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
		                       sub->surface, sub->pos);
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
        if (!output)
	        return;

	wl_list_insert(output->views.prev,
	               &surface->links[TW_VIEW_OUTPUT_LINK]);

	wl_list_for_each(sub, &surface->subsurfaces, parent_link)
		surface_add_to_outputs_list(ctx, sub->surface);
}

WL_EXPORT void
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


WL_EXPORT void
tw_render_context_set_compositor(struct tw_render_context *ctx,
                                 struct tw_compositor *compositor)
{
	wl_signal_emit(&ctx->signals.compositor_set, compositor);
	compositor->obj_alloc = &tw_render_compositor_allocator;
}
