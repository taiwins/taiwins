/*
 * egl_texture.c - taiwins renderer texture functions
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

#include <EGL/egl.h>
#include <assert.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <stdlib.h>
#include <drm_fourcc.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/utils.h>

#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "egl_render_context.h"
#include "pixman.h"
#include "render_context.h"
#include "taiwins/objects/surface.h"

static inline bool
wl_format_supported(struct tw_egl_render_context *ctx,
                    enum wl_shm_format format)
{
	enum wl_shm_format *f;
	wl_array_for_each(f, &ctx->pixel_formats)
		if (format == *f)
			return true;
	return false;
}

//TODO: having all these functions scattered is error prone, we would want to
//have a centralized structure like wlroots
static inline GLuint
wl_format_to_gl_format(enum wl_shm_format format)
{
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
	case WL_SHM_FORMAT_XRGB8888:
		return GL_BGRA_EXT;
	case WL_SHM_FORMAT_ABGR8888:
	case WL_SHM_FORMAT_XBGR8888:
		return GL_RGBA;
	default:
		assert(0);
		return GL_ZERO;
	}
}

static inline bool
wl_format_has_alpha(enum wl_shm_format format)
{
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
	case WL_SHM_FORMAT_ABGR8888:
		return true;
	case WL_SHM_FORMAT_XRGB8888:
	case WL_SHM_FORMAT_XBGR8888:
		return false;
	default:
		assert(0);
		return false;
	}
}


/******************************************************************************
 * texture import
 *****************************************************************************/

static bool
texture_init_pixels(struct tw_egl_render_texture *texture,
                    struct tw_egl_render_context *ctx,
                    struct wl_shm_buffer *buffer)
{
	uint32_t width, height, stride;
	enum wl_shm_format format;
	GLuint glfmt;

	tw_egl_make_current(&ctx->egl, EGL_NO_SURFACE);

	format = wl_shm_buffer_get_format(buffer);
	width = wl_shm_buffer_get_width(buffer);
	height = wl_shm_buffer_get_height(buffer);
	stride = wl_shm_buffer_get_stride(buffer);
	if (!wl_format_supported(ctx, format))
		return false;
	glfmt = wl_format_to_gl_format(format);

	texture->target = GL_TEXTURE_2D;
	texture->base.width = width;
	texture->base.height = height;
	texture->base.wl_format = format;
	texture->base.has_alpha = wl_format_has_alpha(format);
	texture->base.inverted_y = false;

	TW_GLES_DEBUG_PUSH(ctx);

	wl_shm_buffer_begin_access(buffer);
	glGenTextures(1, &texture->gltex);
	glBindTexture(texture->target, texture->gltex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / 4);
	glTexImage2D(texture->target, 0, glfmt,
	             width, height, 0, glfmt,
	             GL_UNSIGNED_BYTE,
	             wl_shm_buffer_get_data(buffer));
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glBindTexture(texture->target, 0);
	wl_shm_buffer_end_access(buffer);

	assert(glGetError() == GL_NO_ERROR);

	TW_GLES_DEBUG_POP(ctx);

	tw_egl_unset_current(&ctx->egl);
	return true;
}

static bool
wl_buffer_is_drm_texture(struct tw_egl *egl, struct wl_resource *buffer)
{
	EGLint fmt;
	//have a function for it/
	return egl->funcs.query_wl_buffer(egl->display, buffer,
	                                  EGL_TEXTURE_FORMAT, &fmt);
}

static bool
texture_init_wl_drm(struct tw_egl_render_texture *texture,
                    struct tw_egl_render_context *ctx,
                    struct wl_resource *buffer)
{
	EGLint fmt;
	int width, height;
	bool inverted_y;
	EGLImageKHR image;

	tw_egl_make_current(&ctx->egl, EGL_NO_SURFACE);

	image = tw_egl_import_wl_drm_image(&ctx->egl, buffer, &fmt,
	                                   &width, &height,
	                                   &inverted_y);
	if (image == EGL_NO_IMAGE_KHR) {
		tw_logl("Failed to create EGL image from wl_drm resource");
		return false;
	}

	switch(fmt) {
	case EGL_TEXTURE_RGB:
		texture->base.has_alpha = false;
		break;
	case EGL_TEXTURE_RGBA:
	case EGL_TEXTURE_EXTERNAL_WL:
		texture->base.has_alpha = true;
		break;
	default:
		tw_logl("invalid EGL buffer type, not supported");
		tw_egl_destroy_image(&ctx->egl, image);
		return false;
	}
	texture->image = image;
	texture->base.width = width;
	texture->base.height = height;
	texture->base.inverted_y = inverted_y;
	texture->base.wl_format = 0xFFFFFFFF;
	texture->target = GL_TEXTURE_EXTERNAL_OES;

	TW_GLES_DEBUG_PUSH(ctx);

	glGenTextures(1, &texture->gltex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->gltex);
	ctx->funcs.image_get_texture2d_oes(GL_TEXTURE_EXTERNAL_OES,
	                                   texture->image);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

	assert(glGetError() == GL_NO_ERROR);

	TW_GLES_DEBUG_POP(ctx);

	tw_egl_unset_current(&ctx->egl);

	return true;
}

static bool
texture_init_dma(struct tw_egl_render_texture *texture,
                 struct tw_egl_render_context *ctx,
                 struct tw_dmabuf_attributes *attrs)
{
	EGLImageKHR image;
	bool external_only = false;

	tw_egl_make_current(&ctx->egl, EGL_NO_SURFACE);

	switch(attrs->format & ~DRM_FORMAT_BIG_ENDIAN) {
	case WL_SHM_FORMAT_YUYV:
	case WL_SHM_FORMAT_YVYU:
	case WL_SHM_FORMAT_UYVY:
	case WL_SHM_FORMAT_VYUY:
	case WL_SHM_FORMAT_AYUV:
		// TODO: YUV based formats not yet supported
		return false;
	default:
		break;
	}
	image = tw_egl_import_dmabuf_image(&ctx->egl, attrs, &external_only);
	if (image == EGL_NO_IMAGE_KHR) {
		tw_logl("failed to import the DMA-BUF image");
		return false;
	}
	texture->image = image;
	texture->base.width = attrs->width;
	texture->base.height = attrs->height;
	texture->base.wl_format = 0xFFFFFFFF;
	texture->base.inverted_y = (attrs->flags &
	                            TW_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) != 0;
	texture->target = GL_TEXTURE_EXTERNAL_OES;

	TW_GLES_DEBUG_PUSH(ctx);

	glGenTextures(1, &texture->gltex);
	glBindTexture(texture->target, texture->gltex);
	ctx->funcs.image_get_texture2d_oes(texture->target,
	                                   texture->image);
	glBindTexture(texture->target, 0);

	assert(glGetError() == GL_NO_ERROR);

	TW_GLES_DEBUG_POP(ctx);

	tw_egl_unset_current(&ctx->egl);
	return true;
}

static bool
texture_update_pixels(struct tw_egl_render_texture *texture,
                      struct tw_egl_render_context *ctx,
                      struct wl_shm_buffer *buffer,
                      uint32_t src_x, uint32_t src_y,
                      uint32_t dst_x, uint32_t dst_y,
                      uint32_t width, uint32_t height)
{
	uint32_t stride;
	enum wl_shm_format format;
	GLuint glfmt;

	tw_egl_make_current(&ctx->egl, EGL_NO_SURFACE);

	format = wl_shm_buffer_get_format(buffer);
	stride = wl_shm_buffer_get_stride(buffer);
	if (!wl_format_supported(ctx, format))
		return false;
	glfmt = wl_format_to_gl_format(format);

	TW_GLES_DEBUG_PUSH(ctx);

	wl_shm_buffer_begin_access(buffer);
	glBindTexture(texture->target, texture->gltex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / 4);;
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, src_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, src_y);

	glTexSubImage2D(texture->target, 0, dst_x, dst_y, width, height,
	                glfmt, GL_UNSIGNED_BYTE,
	                wl_shm_buffer_get_data(buffer));

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);

	glBindTexture(texture->target, 0);
	wl_shm_buffer_end_access(buffer);

	TW_GLES_DEBUG_POP(ctx);

	tw_egl_unset_current(&ctx->egl);
	return true;
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

/******************************************************************************
 * APIs
 *****************************************************************************/

bool
tw_egl_render_texture_init(struct tw_egl_render_texture *texture,
                           struct tw_egl_render_context *ctx,
                           struct wl_resource *buffer)
{
	bool is_wl_drm, is_shm, is_dma;
	struct wl_shm_buffer *shmbuf;
	struct tw_dmabuf_buffer *dmabuf;

	is_shm = wl_shm_buffer_get(buffer) != NULL;
	is_wl_drm = wl_buffer_is_drm_texture(&ctx->egl, buffer);
	is_dma = tw_is_wl_buffer_dmabuf(buffer);

        if (!ctx->funcs.image_get_texture2d_oes && (is_wl_drm || is_dma))
		return false;

        if (is_shm) {
	        shmbuf = wl_shm_buffer_get(buffer);
	        return texture_init_pixels(texture, ctx, shmbuf);
        } else if (is_wl_drm) {
	        return texture_init_wl_drm(texture, ctx, buffer);
        } else if (is_dma) {
	        dmabuf = tw_dmabuf_buffer_from_resource(buffer);
	        return texture_init_dma(texture, ctx, &dmabuf->attributes);
        } else  {
	        return false;
        }
        return true;
}

static bool
tw_egl_render_texture_update(struct tw_egl_render_texture *texture,
                             struct tw_egl_render_context *ctx,
                             struct wl_resource *wl_buffer,
                             pixman_region32_t *update_damage,
                             struct tw_surface_buffer *buffer)
{
	bool ret = true;
	pixman_region32_t all_damage, *damages;
	int n;
	pixman_box32_t *rects, *r;
	struct wl_shm_buffer *shmbuf = wl_shm_buffer_get(wl_buffer);

	if (!shm_buffer_compatible(shmbuf, buffer))
		return false;

	//copy data
	pixman_region32_init_rect(&all_damage, 0, 0,
	                          buffer->width, buffer->height);
	wl_shm_buffer_begin_access(shmbuf);
	damages = (update_damage) ? update_damage : &all_damage;
	rects = pixman_region32_rectangles(damages, &n);
	for (int i = 0; i < n; i++) {
		r = &rects[i];
		if (!texture_update_pixels(texture, ctx, shmbuf,
		                           r->x1, r->y1,
		                           r->x1, r->y1,
		                           r->x2-r->x1, r->y2-r->y1)) {
			ret = false;
			goto out;
		}
	}
out:
	wl_shm_buffer_end_access(shmbuf);
	pixman_region32_fini(&all_damage);
	return ret;
}

/******************************************************************************
 * texture creation
 *****************************************************************************/

static void
tw_egl_render_texture_destroy(struct tw_render_texture *texture,
                              struct tw_render_context *base)
{
	struct tw_egl_render_context *ctx =
		wl_container_of(base, ctx, base);
	struct tw_egl_render_texture *egl_texture =
		wl_container_of(texture, egl_texture, base);

	tw_egl_make_current(&ctx->egl, EGL_NO_SURFACE);

        TW_GLES_DEBUG_PUSH(ctx);
	glDeleteTextures(1, &egl_texture->gltex);
	tw_egl_destroy_image(&ctx->egl, egl_texture->image);
	TW_GLES_DEBUG_POP(ctx);

	tw_egl_unset_current(&ctx->egl);

        free(egl_texture);
}

struct tw_egl_render_texture *
tw_egl_render_texture_new(struct tw_render_context *base,
                          struct wl_resource *res)
{
	struct tw_egl_render_texture *texture = calloc(1, sizeof(*texture));
	struct tw_egl_render_context *ctx = wl_container_of(base, ctx, base);

	if (!texture)
		return NULL;
	if (!tw_egl_render_texture_init(texture, ctx, res)) {
		free(texture);
		return NULL;
	}

	texture->base.destroy = tw_egl_render_texture_destroy;
	texture->ctx = ctx;
	return texture;
}

static void
notify_buffer_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_surface_buffer *buffer =
		wl_container_of(listener, buffer, surface_destroy_listener);
	struct tw_surface *surface =
		wl_container_of(buffer, surface, buffer);
	struct tw_egl_render_texture *texture;
	if (tw_surface_has_texture(surface)) {
		texture = buffer->handle.ptr;
		tw_egl_render_texture_destroy(&texture->base,
		                              &texture->ctx->base);
	}
}

bool
tw_egl_render_context_import_buffer(struct tw_event_buffer_uploading *event,
                                    void *callback)
{
	struct tw_egl_render_context *ctx = callback;
	struct tw_surface *surface =
		wl_container_of(event->buffer, surface, buffer);
	struct tw_egl_render_texture *texture;
	struct tw_egl_render_texture *old_texture = surface->buffer.handle.ptr;
	struct tw_surface_buffer *buffer = event->buffer;

	if (!event->new_upload)
		return tw_egl_render_texture_update(old_texture, ctx,
		                                    event->wl_buffer,
		                                    event->damages, buffer);
	texture = tw_egl_render_texture_new(&ctx->base, event->wl_buffer);
	if (!texture) {
		tw_logl_level(TW_LOG_WARN, "EE: failed to update the texture");
		return false;
	}
	event->buffer->handle.ptr = texture;
	event->buffer->width = texture->base.width;
	event->buffer->height = texture->base.height;

	if (old_texture)
		tw_egl_render_texture_destroy(&old_texture->base, &ctx->base);
        tw_reset_wl_list(&buffer->surface_destroy_listener.link);
        tw_signal_setup_listener(&surface->events.destroy,
                                 &buffer->surface_destroy_listener,
                                 notify_buffer_surface_destroy);
        return true;
}
