/*
 * renderer.c - taiwins backend renderer functions
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

#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>

#include "ctypes/helpers.h"
#include "objects/dmabuf.h"
#include "objects/logger.h"
#include "objects/surface.h"
#include "pixman.h"
#include "shaders.h"
#include "renderer.h"
#include "wlr/render/drm_format_set.h"

static const EGLint gles3_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
	EGL_NONE,
};

static const EGLint gles2_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE,
};

/******************************************************************************
 * interface
 *****************************************************************************/

static const struct wlr_renderer_impl renderer_impl = {

};

static inline bool
check_externsion(const char *exts, const char *ext)
{
	//you can use strstr, or strtok_r to query the strings
	return strstr(exts, ext) != NULL;
}


/**
 * for completing the renderer, we would setup the callbacks, and most
 * importantly, creating the shaders and vao, vbos
 */
static void
init_renderer_interface(struct tw_renderer *renderer, struct wlr_egl *egl)
{
	struct tw_quad_color_shader color_shader;
	struct tw_quad_tex_shader tex_shader, blur_shader;

	wlr_renderer_init(&renderer->base, &renderer_impl);
	wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);

	//check extensions.
	//in our case we may need different extensions to get EGLImage, for now
	//lets skip.

	tw_quad_color_shader_init(&color_shader);
	tw_quad_tex_blend_shader_init(&tex_shader);
	tw_quad_tex_blur_shader_init(&blur_shader);

	tw_quad_color_shader_fini(&color_shader);
	tw_quad_tex_blend_shader_fini(&tex_shader);
	tw_quad_tex_blur_shader_fini(&blur_shader);
}

struct wlr_renderer *
tw_renderer_create(struct wlr_egl *egl, EGLenum platform,
                   void *remote_display, EGLint *config_attribs,
                   EGLint visual_id)
{
	bool init = false;
	struct tw_renderer *renderer = calloc(1, sizeof(struct tw_renderer));
	if (!renderer)
		return NULL;

	init = wlr_egl_init(egl, platform, remote_display,
	                    gles3_config_attribs, visual_id);
	if (!init)
		init = wlr_egl_init(egl, platform, remote_display,
		                    gles2_config_attribs, visual_id);
	if (!init) {
		//the initialization code does plenty of work in
		//eglInitialize, eglQueryString, bind function pointers
		//I am not sure if you can call it twice though
		return NULL;
	}
	init_renderer_interface(renderer, egl);

	wl_signal_init(&renderer->events.pre_output_render);
	wl_signal_init(&renderer->events.post_ouptut_render);
	wl_signal_init(&renderer->events.pre_view_render);
	wl_signal_init(&renderer->events.post_view_render);

	return &renderer->base;
}

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
	return (!shmbuf ||
		(wl_shm_buffer_get_format(shmbuf) != buffer->format) ||
		(wl_shm_buffer_get_stride(shmbuf) != buffer->stride) ||
		(wl_shm_buffer_get_width(shmbuf)  != buffer->width) ||
	        (wl_shm_buffer_get_height(shmbuf) != buffer->height));
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

	if (shm_buffer_compatible(shmbuf, buffer))
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
		                              buffer->width,
		                              buffer->height,
		                              r->x2 - r->x1,
		                              r->y2 - r->y1,
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

void
tw_renderer_import_buffer(struct tw_event_buffer_uploading *event,
                          void *data)
{
	struct wlr_renderer *renderer = data;
	struct wl_shm_buffer *shm_buffer =
		wl_shm_buffer_get(event->wl_buffer);
	struct wlr_texture *texture = NULL;
	struct tw_surface *surface =
		container_of(event->buffer, struct tw_surface, buffer);
	struct tw_surface_buffer *buffer = event->buffer;
	struct tw_dmabuf_buffer *dmabuf;

	//updating texture
	if (tw_surface_has_texture(surface)) {
		update_texture(event, renderer, shm_buffer);
		return;
	}
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
	// updating the
	event->buffer->handle.ptr = texture;
	if (!texture) {
		tw_logl("EE: failed to update the texture");
		return;
	}

	wl_list_remove(&buffer->surface_destroy_listener.link);
	wl_list_init(&buffer->surface_destroy_listener.link);
	buffer->surface_destroy_listener.notify =
		notify_buffer_on_surface_destroy;
	wl_signal_add(&surface->events.destroy,
	              &buffer->surface_destroy_listener);

}

bool
tw_renderer_test_import_dmabuf(struct tw_dmabuf_attributes *attrs,
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

void
tw_renderer_format_request(struct tw_linux_dmabuf *dmabuf,
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

void
tw_renderer_modifiers_request(struct tw_linux_dmabuf *dmabuf,
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
