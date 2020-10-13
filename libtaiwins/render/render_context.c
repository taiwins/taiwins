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

#include <assert.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/profiling.h>

/******************************************************************************
 * tw_render_context Public APIs
 *****************************************************************************/

#define TW_VIEW_GLOBAL_LINK 1
#define TW_VIEW_OUTPUT_LINK 2


static void
notify_tw_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_render_wl_surface *surface =
		wl_container_of(listener, surface, listeners.destroy);
	assert(data == surface->surface);
	wl_signal_emit(&surface->ctx->events.wl_surface_destroy, data);
	tw_render_fini_wl_surface(surface);

	free(surface);
}

static void
notify_tw_surface_dirty(struct wl_listener *listener, void *data)
{
	struct tw_render_wl_surface *surface =
		wl_container_of(listener, surface, listeners.dirty);
	assert(data == surface->surface);
	//forwarding the dirty event.
	wl_signal_emit(&surface->ctx->events.wl_surface_dirty, data);
}

void
tw_render_init_wl_surface(struct tw_render_wl_surface *surface,
                          struct tw_surface *tw_surface,
                          struct tw_render_context *ctx)
{
	wl_list_init(&surface->layer_link);
	wl_list_init(&surface->listeners.commit.link);
	wl_list_init(&surface->listeners.destroy.link);
	wl_list_init(&surface->listeners.dirty.link);
	wl_list_init(&surface->listeners.frame.link);

	pixman_region32_init(&surface->clip);
	surface->surface = tw_surface;
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
}

void
tw_render_fini_wl_surface(struct tw_render_wl_surface *surface)
{
	wl_list_remove(&surface->layer_link);
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

/** I think this will simply seat here even if we have a vulkan render context,
 EGL render context will be compulsory */
void
tw_render_context_destroy(struct tw_render_context *ctx)
{
	ctx->display_destroy.notify(&ctx->display_destroy, ctx->display);
}

int
tw_render_presentable_make_current(struct tw_render_presentable *surf,
                                   struct tw_render_context *ctx)
{
	assert(ctx->impl && ctx->impl->commit_surface);
	return ctx->impl->make_current(surf, ctx);
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

        wl_list_for_each(tmp, &ctx->outputs, link) {
		if (tmp->device.id == surface->output) {
			output = tmp;
			break;
		}
	}
	assert(output);
	assert(surface->output >= 0 && surface->output <= 31);

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
