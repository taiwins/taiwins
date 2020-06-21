/*
 * taiwins.c - taiwins server shared functions
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include "wlr/types/wlr_matrix.h"
#endif

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>
#include <wayland-server.h>
#include <libweston/libweston.h>
#include <wlr/types/wlr_matrix.h>
#include <ctypes/os/os-compatibility.h>

#include <backend/render/renderer.h>
#include <backend/backend.h>
#include <objects/surface.h>
#include <objects/compositor.h>
#include "taiwins.h"


void
tw_lose_surface_focus(struct weston_surface *surface)
{
	struct weston_compositor *ec = surface->compositor;
	struct weston_seat *seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(seat, &ec->seat_list, link) {
		 keyboard = weston_seat_get_keyboard(seat);
		if (keyboard &&
		    (weston_surface_get_main_surface(keyboard->focus) == surface))
			weston_keyboard_set_focus(keyboard, NULL);
		//it maynot be a good idea to do the pointer and touch as well,
		//since FIRST only keyboard gets the focus of a surface, the
		//rest gets the focus from view; SECOND if we do this when we
		//need focused output, there is no thing we can do
	}
}

void
tw_focus_surface(struct weston_surface *surface)
{
	struct weston_seat *active_seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(active_seat, &surface->compositor->seat_list, link) {
		keyboard = active_seat->keyboard_state;
		if (keyboard) {
			weston_keyboard_set_focus(keyboard, surface);
			break;
		}
	}
}

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_touch *touch = weston_seat_get_touch(seat);
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		/* Priority has touch focus, then pointer and
		 * then keyboard focus. We should probably have
		 * three for loops and check frist for touch,
		 * then for pointer, etc. but unless somebody has some
		 * objections, I think this is sufficient. */
		if (touch && touch->focus)
			output = touch->focus->output;
		else if (pointer && pointer->focus)
			output = pointer->focus->output;
		else if (keyboard && keyboard->focus)
			output = keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

void *
tw_load_weston_module(const char *name, const char *entrypoint)
{
	void *module, *init;

	if (name == NULL || entrypoint == NULL)
		return NULL;

	//our modules are in the rpath as we do not have the
	//LIBWESTON_MODULEDIR, so we need to test name and
	module = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
	if (module) {
		tw_logl("Module '%s' already loaded\n", name);
		return NULL;
	} else {
		module = dlopen(name, RTLD_NOW);
		if (!module) {
			tw_logl("Failed to load the module %s\n", name);
			return NULL;
		}
	}

	init = dlsym(module, entrypoint);
	if (!init) {
		tw_logl("Faild to lookup function in module: %s\n",
		           dlerror());
		dlclose(module);
		return NULL;
	}
	return init;

}

/******************************************************************************
 * renderer bindings
 *****************************************************************************/

static void
notify_buffer_on_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_surface_buffer *buffer =
		container_of(listener, struct tw_surface_buffer,
		             surface_destroy_listener);
	struct tw_surface *surface =
		container_of(buffer, struct tw_surface, buffer);
	if (tw_surface_has_texture(surface)) {
		wlr_texture_destroy(buffer->handle.ptr);
	}
}

static bool
shm_buffer_compatible(struct wl_shm_buffer *shmbuf,
                      struct tw_surface_buffer *buffer)
{
	return (shmbuf &&
		(wl_shm_buffer_get_format(shmbuf) == buffer->format) &&
		(wl_shm_buffer_get_stride(shmbuf) == buffer->stride) &&
		(wl_shm_buffer_get_width(shmbuf)  == buffer->width) &&
	        (wl_shm_buffer_get_height(shmbuf) == buffer->height));
}

static bool
update_texture(struct tw_event_buffer_uploading *event,
               struct wlr_renderer *renderer,
               struct wl_shm_buffer *shmbuf)
{
	bool ret = true;
	struct tw_surface_buffer *buffer = event->buffer;
	struct wlr_texture *texture = buffer->handle.ptr;
	pixman_region32_t all_damage, *damages;
	void *data;
	int n;
	pixman_box32_t *rects, *r;

	if (!shm_buffer_compatible(shmbuf, buffer))
		return false;

	//copy data
	pixman_region32_init_rect(&all_damage, 0, 0,
	                          buffer->width, buffer->height);
	wl_shm_buffer_begin_access(shmbuf);
	data = wl_shm_buffer_get_data(shmbuf);
	damages = (event->damages) ? event->damages : &all_damage;
	rects = pixman_region32_rectangles(damages, &n);
	for (int i = 0; i < n; i++) {
		r = &rects[i];
		if (!wlr_texture_write_pixels(texture, buffer->stride,
		                              r->x2-r->x1,
		                              r->y2-r->y1,
		                              r->x1, r->y1,
		                              r->x1, r->y1, data)) {
			ret = false;
			goto out;
		}
	}
out:
	wl_shm_buffer_end_access(shmbuf);
	pixman_region32_fini(&all_damage);
	return ret;
}

static struct wlr_texture *
new_shm_texture(struct wl_shm_buffer *shm_buffer,
                struct tw_surface_buffer *buffer,
                struct wlr_renderer *renderer)
{
	struct wlr_texture *texture = NULL;
	void *shm_data;
	buffer->format = wl_shm_buffer_get_format(shm_buffer);
	buffer->width = wl_shm_buffer_get_width(shm_buffer);
	buffer->height = wl_shm_buffer_get_height(shm_buffer);
	buffer->stride = wl_shm_buffer_get_stride(shm_buffer);
	wl_shm_buffer_begin_access(shm_buffer);
	shm_data = wl_shm_buffer_get_data(shm_buffer);
	texture = wlr_texture_from_pixels(renderer,
	                                  buffer->format,
	                                  buffer->stride,
	                                  buffer->width,
	                                  buffer->height,
	                                  shm_data);
	wl_shm_buffer_end_access(shm_buffer);
	return texture;
}

static struct wlr_texture *
new_dma_texture(struct tw_dmabuf_attributes *attributes,
                struct wlr_renderer *renderer)
{
	struct wlr_dmabuf_attributes attr;

	attr.n_planes = attributes->n_planes;
	for (int i = 0; i < attr.n_planes; i++) {
		attr.fd[i] = attributes->fds[i];
		attr.offset[i] = attributes->offsets[i];
		attr.stride[i] = attributes->strides[i];
	}
	attr.flags = attributes->flags;
	attr.format = attributes->format;
	attr.modifier = attributes->modifier;
	attr.width = attributes->width;
	attr.height = attributes->height;

	return wlr_texture_from_dmabuf(renderer, &attr);
}

static bool
renderer_import_buffer(struct tw_event_buffer_uploading *event,
                       void *data)
{
	struct wlr_renderer *renderer = data;
	struct wl_shm_buffer *shm_buffer =
		wl_shm_buffer_get(event->wl_buffer);
	struct tw_surface *surface =
		container_of(event->buffer, struct tw_surface, buffer);
	struct wlr_texture *texture = NULL;
	struct wlr_texture *old_texture = surface->buffer.handle.ptr;
	struct tw_surface_buffer *buffer = event->buffer;
	struct tw_dmabuf_buffer *dmabuf;

	//updating could fail due to all kinds of imcompatible issues.
	if (!event->new_upload)
		return update_texture(event, renderer, shm_buffer);
	//new texture
	if (shm_buffer) {
		texture = new_shm_texture(shm_buffer, buffer, renderer);
	} else if (wlr_renderer_resource_is_wl_drm_buffer(renderer,
	                                                  event->wl_buffer)) {
		texture = wlr_texture_from_wl_drm(renderer,
		                                  event->wl_buffer);
		wlr_texture_get_size(texture, &buffer->width, &buffer->height);
	} else if (tw_is_wl_buffer_dmabuf(event->wl_buffer)) {
		dmabuf = tw_dmabuf_buffer_from_resource(event->wl_buffer);
		texture = new_dma_texture(&dmabuf->attributes, renderer);
		wlr_texture_get_size(texture, &buffer->width, &buffer->height);
	} else {
		tw_logl("EE: Cannot upload texture, unknown buffer type");
		wl_resource_post_error(event->wl_buffer, 0,
		                       "unknown buffer type");
	}
	if (!texture) {
		tw_logl("EE: failed to update the texture");
		return false;
	}
	event->buffer->handle.ptr = texture;
	if (old_texture)
		wlr_texture_destroy(old_texture);

	wl_list_remove(&buffer->surface_destroy_listener.link);
	wl_list_init(&buffer->surface_destroy_listener.link);
	buffer->surface_destroy_listener.notify =
		notify_buffer_on_surface_destroy;
	wl_signal_add(&surface->events.destroy,
	              &buffer->surface_destroy_listener);
	return true;
}

/************************* dma engine bindings *******************************/

static bool
renderer_test_import_dmabuf(struct tw_dmabuf_attributes *attrs,
                               void *data)
{
	struct wlr_renderer *renderer = data;
	struct wlr_texture *texture = new_dma_texture(attrs, renderer);
	if (texture) {
		wlr_texture_destroy(texture);
		return true;
	} else {
		return false;
	}
}

static void
renderer_format_request(struct tw_linux_dmabuf *dmabuf,
                           void *callback, int *formats,
                           size_t *n_formats)
{
	struct wlr_renderer *renderer = callback;
	const struct wlr_drm_format_set *format_set =
		wlr_renderer_get_dmabuf_formats(renderer);
	if (!format_set) {
		*n_formats = 0;
		return;
	}
	*n_formats = format_set->len;
	if (formats) {
		for (unsigned i = 0; i < *n_formats; i++)
			formats[i] = format_set->formats[i]->format;
	}
}

static void
renderer_modifiers_request(struct tw_linux_dmabuf *dmabuf,
                           void *callback, int format,
                           uint64_t *modifiers,
                           size_t *n_modifiers)
{
	struct wlr_renderer *renderer = callback;
	const struct wlr_drm_format_set *format_set =
		wlr_renderer_get_dmabuf_formats(renderer);
	if (!format_set)
		goto no_modifiers;

	struct wlr_drm_format *tmp, *fmt = NULL;
	for (unsigned i = 0; i < format_set->len; i++) {
		tmp = format_set->formats[i];
		if (tmp->format == (unsigned)format) {
			fmt = tmp;
			break;
		}
	}
	//retrieve the modifiers of the format.
	if (!fmt)
		goto no_modifiers;
	*n_modifiers = fmt->len;
	if (*n_modifiers && modifiers) {
		for (unsigned i = 0; i < fmt->len; i++)
			modifiers[i] = fmt->modifiers[i];
	}

no_modifiers:
	*n_modifiers = 0;
	return;

}

/******************************************************************************
 * tw_server, this is probably a bad place to set it up
 *****************************************************************************/
static void
render_surface_texture(struct tw_surface *surface,
                       struct wlr_renderer *renderer,
                       struct wlr_output *wlr_output)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	//TODO, wlr_matrix is row major and the it uses a total different
	//coordinate system, sadly I cannot take advantage of it.

	float transform[9];
	struct wlr_texture *texture = surface->buffer.handle.ptr;
	if (texture) {
		wlr_matrix_transpose(transform,
		                     surface->geometry.transform.d);
		wlr_matrix_multiply(transform,
		                    wlr_output->transform_matrix,
		                    transform);
		/* wlr_render_texture_with_matrix(renderer, texture, */
		/*                                transform, 1.0); */
		wlr_render_texture(renderer, texture,
		                   wlr_output->transform_matrix,
		                   surface->geometry.xywh.x,
		                   surface->geometry.xywh.y,
		                   1.0f);
	}
	tw_surface_flush_frame(surface,
	                       now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static void
notify_new_output_frame(struct wl_listener *listener, void *data)
{
	int width, height;
	struct tw_server *server =
		container_of(listener, struct tw_server, output_frame);
	struct tw_backend_output *output = data;
	struct wlr_renderer *renderer = server->wlr_renderer;
	struct wlr_output *wlr_output = output->wlr_output;
	struct tw_surface *surface;

	tw_server_build_surface_list(server);
	/* tw_server_stack_damage(server); */

	if (!wlr_output_attach_render(output->wlr_output, NULL))
		return;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	wlr_renderer_begin(renderer, width, height);

        float color[4] = {0.3, 0.3, 0.3, 1.0};
	wlr_renderer_clear(renderer, color);

	wl_list_for_each(surface, &server->layers_manager.views,
	                 links[TW_VIEW_SERVER_LINK])
		render_surface_texture(surface, renderer, wlr_output);

	wlr_renderer_end(renderer);
	wlr_output_commit(output->wlr_output);
}

static void
notify_create_wl_surface(struct wl_listener *listener, void *data)
{
	struct tw_surface *surface;
	struct tw_server *server =
		container_of(listener, struct tw_server,
		             surface_create_listener);
	struct tw_event_new_wl_surface *event = data;

	surface = tw_surface_create(event->client, event->version, event->id,
	                            &server->surface_manager);
	//TODO: remove this: this is temporary code
	wl_list_insert(server->layers_manager.cursor_layer.views.prev,
	               &surface->links[TW_VIEW_LAYER_LINK]);
}

static void
notify_dirty_wl_surface(struct wl_listener *listener, void *data)
{
	struct tw_backend_output *output;
	struct tw_server *server =
		container_of(listener, struct tw_server,
		             surface_dirty_listener);
	struct tw_surface *surface = data;

	wl_list_for_each(output, &server->backend->heads, link) {
		if ((1u << output->id) & surface->output_mask)
			tw_backend_output_dirty(output);
	}
}

static void
notify_destroy_wl_surface(struct wl_listener *listener, void *data)
{
	//TODO: when a surface unmaps, its clip region is uncovered, thus the
	//related output needs to repaint, what we do is totally run here, need
	//to fix it later

	struct tw_server *server =
		container_of(listener, struct tw_server,
		             surface_destroy_listener);
	notify_dirty_wl_surface(&server->surface_dirty_listener, data);
}

static void
notify_create_wl_subsurface(struct wl_listener *listener, void *data)
{
	struct tw_event_get_wl_subsurface *event = data;
	struct tw_surface *surface, *parent;
	surface = tw_surface_from_resource(event->surface);
	parent = tw_surface_from_resource(event->parent_surface);

	tw_subsurface_create(event->client, event->version, event->id,
	                     surface, parent);
}

static void
notify_create_wl_region(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server,
		             region_create_listener);
	struct tw_event_new_wl_region *event = data;
	tw_region_create(event->client, event->version, event->id,
	                 &server->surface_manager);
}

static void
notify_adding_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_add);
	struct tw_backend_seat *seat = data;
	uint32_t i = seat->idx;
	tw_seat_events_init(&server->seat_events[i], seat,
	                    server->binding_state);
}

static void
notify_removing_seat(struct wl_listener *listener, void *data)
{
	struct tw_server *server =
		container_of(listener, struct tw_server, seat_remove);
	struct tw_backend_seat *seat = data;
	uint32_t i = seat->idx;
	tw_seat_events_fini(&server->seat_events[i]);
}

static void
bind_listeners(struct tw_server *server)
{
	//seat add
	wl_list_init(&server->seat_add.link);
	server->seat_add.notify = notify_adding_seat;
	wl_signal_add(&server->backend->seat_add_signal,
	              &server->seat_add);
	//seat remove
	wl_list_init(&server->seat_remove.link);
	server->seat_remove.notify = notify_removing_seat;
	wl_signal_add(&server->backend->seat_rm_signal,
	              &server->seat_remove);
	//create wl_surface
	wl_list_init(&server->surface_create_listener.link);
	server->surface_create_listener.notify = notify_create_wl_surface;
	wl_signal_add(&server->compositor->surface_create,
	              &server->surface_create_listener);
	//destroy wl_surface
	wl_list_init(&server->surface_destroy_listener.link);
	server->surface_destroy_listener.notify = notify_destroy_wl_surface;
	wl_signal_add(&server->surface_manager.surface_destroy_signal,
	              &server->surface_destroy_listener);
	//dirty wl_surface
	wl_list_init(&server->surface_dirty_listener.link);
	server->surface_dirty_listener.notify = notify_dirty_wl_surface;
	wl_signal_add(&server->surface_manager.surface_dirty_signal,
	              &server->surface_dirty_listener);
	//create wl_subsurface
	wl_list_init(&server->subsurface_create_listener.link);
	server->subsurface_create_listener.notify =
		notify_create_wl_subsurface;
	wl_signal_add(&server->compositor->subsurface_get,
	              &server->subsurface_create_listener);
	//create wl_region
	wl_list_init(&server->region_create_listener.link);
	server->region_create_listener.notify =
		notify_create_wl_region;
	wl_signal_add(&server->compositor->region_create,
	              &server->region_create_listener);

	//the frame callback, here we could have a choice in the future, if
	//renderer offers different frame type.
	wl_list_init(&server->output_frame.link);
	server->output_frame.notify = notify_new_output_frame;
	wl_signal_add(&server->backend->output_frame_signal,
	              &server->output_frame);

}

static bool
bind_backend(struct tw_server *server)
{
	//handle backend
	server->backend = tw_backend_create_global(server->display);
	if (!server->backend) {
		tw_logl("EE: failed to create backend\n");
		return false;
	}
	tw_backend_defer_outputs(server->backend, true);

	server->wlr_backend = tw_backend_get_backend(server->backend);
	server->wlr_renderer = wlr_backend_get_renderer(server->wlr_backend);
	return true;
}

static void
bind_globals(struct tw_server *server)
{
	//declare various globals
	server->compositor =
		tw_compositor_create_global(server->display);
	// cant use it, would need wlr_seat
	wl_display_init_shm(server->display);

	server->dma_engine = tw_dmabuf_create_global(server->display);

	server->data_device = tw_data_device_create_global(server->display);

	tw_surface_manager_init(&server->surface_manager);

	tw_layers_manager_init(&server->layers_manager, server->display);

	//bindinds for renderer
	if (server->wlr_renderer) {
		//import buffer for surface
		server->surface_manager.buffer_import.buffer_import =
			renderer_import_buffer;
		server->surface_manager.buffer_import.callback =
			server->wlr_renderer;
		//dma engine
		server->dma_engine->import_buffer.import_buffer =
			renderer_test_import_dmabuf;
		server->dma_engine->import_buffer.callback =
			server->wlr_renderer;
		server->dma_engine->format_request.format_request =
			renderer_format_request;
		server->dma_engine->format_request.modifiers_request =
			renderer_modifiers_request;
		server->dma_engine->format_request.callback =
			server->wlr_renderer;
	}

	//bindings
	server->binding_state =
		tw_bindings_create(server->display);
	tw_bindings_add_dummy(server->binding_state);
}

bool
tw_server_init(struct tw_server *server, struct wl_display *display)
{
	server->display = display;
	server->loop = wl_display_get_event_loop(display);
	if (!bind_backend(server))
		return false;
	bind_globals(server);
	bind_listeners(server);

	return true;
}
