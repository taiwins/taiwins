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
#include <render_context.h>

/******************************************************************************
 * tw_render_context Public APIs
 *****************************************************************************/

/** I think this will simply seat here even if we have a vulkan render context,
 EGL render context will be compulsory */
void
tw_render_context_destroy(struct tw_render_context *ctx)
{
	ctx->display_destroy.notify(&ctx->display_destroy, ctx->display);
}

int
tw_render_surface_make_current(struct tw_render_surface *surf,
                               struct tw_render_context *ctx)
{
	assert(ctx->impl && ctx->impl->commit_surface);
	return ctx->impl->make_current(surf, ctx);
}

void
tw_render_context_set_dma(struct tw_render_context *ctx,
                          struct tw_linux_dmabuf *dma)
{
	wl_signal_emit(&ctx->events.dma_set, dma);
}

void
tw_render_context_set_compositor(struct tw_render_context *ctx,
                                 struct tw_compositor *compositor)
{
	wl_signal_emit(&ctx->events.compositor_set, compositor);
}

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
