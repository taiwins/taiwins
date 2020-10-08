/*
 * objects_proxy.c - proxy to taiwins objects
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
#include <ctypes/helpers.h>
#include <fcntl.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>

#include "backend.h"
#include "backend_internal.h"
#include "renderer/renderer.h"

/* /\****************************************************************************** */
/*  * buffer imports */
/*  *****************************************************************************\/ */

/* static void */
/* notify_buffer_on_surface_destroy(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_surface_buffer *buffer = */
/*		container_of(listener, struct tw_surface_buffer, */
/*		             surface_destroy_listener); */
/*	struct tw_surface *surface = */
/*		container_of(buffer, struct tw_surface, buffer); */
/*	if (tw_surface_has_texture(surface)) */
/*		tw_render_texture_destroy(buffer->handle.ptr); */
/* } */

/* static bool */
/* shm_buffer_compatible(struct wl_shm_buffer *shmbuf, */
/*                       struct tw_surface_buffer *buffer) */
/* { */
/*	return (shmbuf && */
/*		(wl_shm_buffer_get_format(shmbuf) == buffer->format) && */
/*		(wl_shm_buffer_get_stride(shmbuf) == buffer->stride) && */
/*		(wl_shm_buffer_get_width(shmbuf)  == buffer->width) && */
/*	        (wl_shm_buffer_get_height(shmbuf) == buffer->height)); */
/* } */

/* static bool */
/* update_texture(struct tw_event_buffer_uploading *event, */
/*                struct tw_renderer *rdr) */
/* { */
/*	bool ret = true; */
/*	struct tw_surface_buffer *buffer = event->buffer; */
/*	struct tw_render_texture *texture = buffer->handle.ptr; */
/*	pixman_region32_t all_damage, *damages; */
/*	int n; */
/*	pixman_box32_t *rects, *r; */
/*	struct wl_shm_buffer *shmbuf = wl_shm_buffer_get(event->wl_buffer); */

/*	if (!shm_buffer_compatible(shmbuf, buffer)) */
/*		return false; */

/*	//copy data */
/*	pixman_region32_init_rect(&all_damage, 0, 0, */
/*	                          buffer->width, buffer->height); */
/*	wl_shm_buffer_begin_access(shmbuf); */
/*	damages = (event->damages) ? event->damages : &all_damage; */
/*	rects = pixman_region32_rectangles(damages, &n); */
/*	for (int i = 0; i < n; i++) { */
/*		r = &rects[i]; */
/*		if (!tw_renderer_texture_update(texture, rdr, shmbuf, */
/*		                                r->x1, r->y1, */
/*		                                r->x1, r->y1, */
/*		                                r->x2-r->x1, r->y2-r->y1)) { */
/*			ret = false; */
/*			goto out; */
/*		} */
/*	} */
/* out: */
/*	wl_shm_buffer_end_access(shmbuf); */
/*	pixman_region32_fini(&all_damage); */
/*	return ret; */
/* } */

/* static bool */
/* renderer_import_buffer(struct tw_event_buffer_uploading *event, */
/*                        void *data) */
/* { */
/*	struct tw_renderer *rdr = data; */
/*	struct tw_surface *surface = */
/*		container_of(event->buffer, struct tw_surface, buffer); */
/*	struct tw_render_texture *texture; */
/*	struct tw_render_texture *old_texture = surface->buffer.handle.ptr; */
/*	struct tw_surface_buffer *buffer = event->buffer; */

/*	//updating could fail due to all kinds of imcompatible issues. */
/*	if (!event->new_upload) */
/*		return update_texture(event, rdr); */

/*	texture = tw_renderer_new_texture(rdr, event->wl_buffer); */

/*	if (!texture) { */
/*		tw_logl_level(TW_LOG_WARN, "EE: failed to update the texture"); */
/*		return false; */
/*	} */
/*	event->buffer->handle.ptr = texture; */
/*	event->buffer->width = texture->width; */
/*	event->buffer->height = texture->height; */

/*	if (old_texture) */
/*		tw_render_texture_destroy(old_texture); */

/*         tw_reset_wl_list(&buffer->surface_destroy_listener.link); */
/*	buffer->surface_destroy_listener.notify = */
/*		notify_buffer_on_surface_destroy; */
/*	wl_signal_add(&surface->events.destroy, */
/*	              &buffer->surface_destroy_listener); */
/*	return true; */
/* } */

/************************* dma engine bindings *******************************/

/******************************************************************************
 * listeners
 *****************************************************************************/
/* static void */
/* update_surface_mask(struct tw_surface *surface, */
/*                     struct tw_backend_output *major, uint32_t mask) */
/* { */
/*	struct tw_backend_output *output; */
/*	struct wl_resource *res; */
/*	struct tw_backend *backend = major->backend; */
/*	uint32_t output_bit; */
/*	uint32_t different = surface->output_mask ^ mask; */
/*	uint32_t entered = mask & different; */
/*	uint32_t left = surface->output_mask & different; */
/*	struct wl_client *client = wl_resource_get_client(surface->resource); */

/*	//update the surface_mask and */
/*	surface->output_mask = mask; */
/*	surface->output = major->id; */

/*	wl_list_for_each(output, &backend->heads, link) { */
/*		output_bit = 1u << output->id; */
/*		if (!(output_bit & different)) */
/*			continue; */
/*		wl_resource_for_each(res, &output->wlr_output->resources) { */
/*			if (client != wl_resource_get_client(res)) */
/*				continue; */
/*			if ((output_bit & entered)) */
/*				wl_surface_send_enter(surface->resource, res); */
/*			if ((output_bit & left)) */
/*				wl_surface_send_leave(surface->resource, res); */
/*		} */
/*	} */
/* } */

/* static void */
/* reassign_surface_outputs(struct tw_surface *surface, */
/*                          struct tw_backend *backend) */
/* { */
/*	uint32_t area = 0, max = 0, mask = 0; */
/*	struct tw_backend_output *output, *major = NULL; */
/*	pixman_region32_t surface_region; */
/*	pixman_box32_t *e; */

/*	pixman_region32_init_rect(&surface_region, */
/*	                          surface->geometry.xywh.x, */
/*	                          surface->geometry.xywh.y, */
/*	                          surface->geometry.xywh.width, */
/*	                          surface->geometry.xywh.height); */
/*	wl_list_for_each(output, &backend->heads, link) { */
/*		pixman_region32_t clip; */

/*		if (output->cloning >= 0) */
/*			continue; */
/*		pixman_region32_init_rect(&clip, */
/*		                          output->state.x, output->state.y, */
/*		                          output->state.w, output->state.h); */
/*		pixman_region32_intersect(&clip, &clip, &surface_region); */
/*		e = pixman_region32_extents(&clip); */
/*		area = (e->x2 - e->x1) * (e->y2 - e->y1); */
/*		if (pixman_region32_not_empty(&clip)) */
/*			mask |= (1u << output->id); */
/*		if (area >= max) { */
/*			major = output; */
/*			max = area; */
/*		} */
/*		pixman_region32_fini(&clip); */
/*	} */
/*	pixman_region32_fini(&surface_region); */

/*	update_surface_mask(surface, major, mask); */
/* } */

/* static void */
/* notify_new_wl_surface(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_backend_obj_proxy *obj_proxy = */
/*		container_of(listener, struct tw_backend_obj_proxy, */
/*		             compositor_create_surface); */
/*	struct tw_backend *backend = obj_proxy->backend; */

/*	struct tw_event_new_wl_surface *event = data; */

/*	tw_surface_create(event->client, event->version, event->id, */
/*	                  &backend->surface_manager); */
/* } */

/* static void */
/* notify_new_wl_subsurface(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_event_get_wl_subsurface *event = data; */
/*	struct tw_surface *surface, *parent; */

/*	surface = tw_surface_from_resource(event->surface); */
/*	parent = tw_surface_from_resource(event->parent_surface); */
/*	tw_subsurface_create(event->client, event->version, event->id, */
/*	                     surface, parent); */
/* } */

/* static void */
/* notify_new_wl_region(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_backend_obj_proxy *obj_proxy = */
/*		container_of(listener, struct tw_backend_obj_proxy, */
/*		             compositor_create_region); */
/*	struct tw_event_new_wl_region *event = data; */
/*	tw_region_create(event->client, event->version, event->id, */
/*	                 &obj_proxy->backend->surface_manager); */
/* } */

/* static void */
/* notify_dirty_wl_surface(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_backend_output *output; */
/*	struct tw_backend_obj_proxy *obj_proxy = */
/*		container_of(listener, struct tw_backend_obj_proxy, */
/*		             surface_dirty_output); */
/*	struct tw_surface *surface = data; */
/*	struct tw_backend *backend = obj_proxy->backend; */

/*	if (pixman_region32_not_empty(&surface->geometry.dirty)) */
/*		reassign_surface_outputs(surface, backend); */

/*	wl_list_for_each(output, &backend->heads, link) { */
/*		if ((1u << output->id) & surface->output_mask) */
/*			tw_backend_output_dirty(output); */
/*	} */
/* } */

/* static void */
/* notify_rm_wl_surface(struct wl_listener *listener, void *data) */
/* { */
/*	struct tw_backend_output *output; */
/*	struct tw_surface *surface = data; */
/*	struct tw_backend_obj_proxy *obj_proxy = */
/*		container_of(listener, struct tw_backend_obj_proxy, */
/*		             surface_destroy); */
/*	struct tw_backend *backend = obj_proxy->backend; */
/*	struct tw_renderer *renderer = */
/*		container_of(backend->main_renderer, struct tw_renderer, base); */

/*	wl_list_for_each(output, &backend->heads, link) { */
/*		if ((1u << output->id) & surface->output_mask) */
/*			tw_backend_output_dirty(output); */
/*	} */
/*	renderer->notify_surface_destroy(renderer, surface); */
/* } */

void
tw_backend_init_obj_proxy(struct tw_backend_obj_proxy *obj_proxy,
                          struct tw_backend *backend)
{
	//implementation is a global object, which can only be initialized once
	struct wlr_backend *auto_backend = backend->auto_backend;
	struct wlr_renderer *main_renderer = backend->main_renderer;
	/* struct tw_renderer *rdr; */

	assert(!obj_proxy->backend);
	assert(auto_backend);
	assert(main_renderer);
	obj_proxy->backend = backend;
	/* rdr = container_of(main_renderer, struct tw_renderer, base); */

	//listeners
	/* tw_signal_setup_listener(&backend->compositor_manager.surface_create, */
	/*                          &obj_proxy->compositor_create_surface, */
	/*                          notify_new_wl_surface); */
	/* tw_signal_setup_listener(&backend->compositor_manager.subsurface_get, */
	/*                          &obj_proxy->compositor_create_subsurface, */
	/*                          notify_new_wl_subsurface); */
	/* tw_signal_setup_listener(&backend->compositor_manager.region_create, */
	/*                          &obj_proxy->compositor_create_region, */
	/*                          notify_new_wl_region); */

	/* wl_list_init(&obj_proxy->surface_destroy.link); */
	/* obj_proxy->surface_destroy.notify = notify_rm_wl_surface; */
	/* wl_signal_add(&backend->surface_manager.surface_destroy_signal, */
	/*               &obj_proxy->surface_destroy); */

	/* wl_list_init(&obj_proxy->surface_dirty_output.link); */
	/* obj_proxy->surface_dirty_output.notify = notify_dirty_wl_surface; */
	/* wl_signal_add(&backend->surface_manager.surface_dirty_signal, */
	/*               &obj_proxy->surface_dirty_output); */

        //buffer imports
	/* backend->surface_manager.buffer_import.buffer_import = */
	/*	renderer_import_buffer; */
	/* backend->surface_manager.buffer_import.callback = rdr; */
		//dma engine
	/* backend->dma_engine.import_buffer.import_buffer = */
	/*	renderer_test_import_dmabuf; */
	/* backend->dma_engine.import_buffer.callback = rdr; */
	/* backend->dma_engine.format_request.format_request = */
	/*	renderer_format_request; */
	/* backend->dma_engine.format_request.modifiers_request = */
	/*	renderer_modifiers_request; */
	/* backend->dma_engine.format_request.callback = rdr; */

	backend->proxy = obj_proxy;
}

void
tw_backend_fini_obj_proxy(struct tw_backend_obj_proxy *obj_proxy)
{
	wl_list_remove(&obj_proxy->compositor_create_surface.link);
	wl_list_remove(&obj_proxy->compositor_create_subsurface.link);
	wl_list_remove(&obj_proxy->compositor_create_region.link);
	wl_list_remove(&obj_proxy->surface_dirty_output.link);
	wl_list_remove(&obj_proxy->surface_destroy.link);

	obj_proxy->backend->proxy = NULL;
	obj_proxy->backend = NULL;
}
