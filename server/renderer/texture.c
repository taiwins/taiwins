/*
 * texture.c - taiwins renderer texture functions
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
#include <drm_fourcc.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/dmabuf.h>
#include <string.h>
#include <wayland-server-core.h>

#include "renderer.h"

static inline bool
wl_format_supported(struct tw_renderer *renderer, enum wl_shm_format format)
{
	enum wl_shm_format *f;
	wl_array_for_each(f, &renderer->pixel_formats)
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

static void
tw_render_texture_fini(struct tw_render_texture *texture)
{
	wlr_egl_make_current(texture->rdr->egl, EGL_NO_SURFACE, NULL);
	glDeleteTextures(1, &texture->gltex);
	wlr_egl_destroy_image(texture->rdr->egl, texture->image);
	wlr_egl_unset_current(texture->rdr->egl);
}

static void
tw_render_texture_free(struct tw_render_texture *texture)
{
	free(texture);
}

void
tw_render_texture_destroy(struct tw_render_texture *texture)
{
	tw_render_texture_fini(texture);
	if (texture->destroy)
		texture->destroy(texture);
}

static bool
tw_render_texture_init_pixels(struct tw_render_texture *texture,
                              struct tw_renderer *rdr,
                              struct wl_shm_buffer *buffer)
{
	uint32_t width, height, stride;
	enum wl_shm_format format;
	GLuint glfmt;

	wlr_egl_make_current(rdr->egl, EGL_NO_SURFACE, NULL);

	format = wl_shm_buffer_get_format(buffer);
	width = wl_shm_buffer_get_width(buffer);
	height = wl_shm_buffer_get_height(buffer);
	stride = wl_shm_buffer_get_stride(buffer);
	if (!wl_format_supported(rdr, format))
		return false;
	glfmt = wl_format_to_gl_format(format);

	texture->rdr = rdr;
	texture->target = GL_TEXTURE_2D;
	texture->width = width;
	texture->height = height;
	texture->wl_format = format;
	texture->has_alpha = wl_format_has_alpha(format);
	texture->inverted_y = false;

	TW_GLES_DEBUG_PUSH();

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

	TW_GLES_DEBUG_POP();

	wlr_egl_unset_current(rdr->egl);
	return true;
}

static bool
wl_buffer_is_drm_texture(struct tw_renderer *rdr, struct wl_resource *buffer)
{
	EGLint fmt;
	return rdr->egl->procs.eglQueryWaylandBufferWL(rdr->egl->display,
	                                               buffer,
	                                               EGL_TEXTURE_FORMAT,
	                                               &fmt);
}

static bool
tw_render_texture_init_wl_drm(struct tw_render_texture *texture,
                              struct tw_renderer *rdr,
                              struct wl_resource *buffer)
{
	EGLint fmt;
	int width, height;
	bool inverted_y;
	EGLImageKHR image;

	wlr_egl_make_current(rdr->egl, EGL_NO_SURFACE, NULL);

	image = wlr_egl_create_image_from_wl_drm(rdr->egl, buffer, &fmt,
	                                         &width, &height,
	                                         &inverted_y);
	if (image == EGL_NO_IMAGE_KHR) {
		tw_logl("Failed to create EGL image from wl_drm resource");
		return false;
	}

	switch(fmt) {
	case EGL_TEXTURE_RGB:
		texture->has_alpha = false;
		break;
	case EGL_TEXTURE_RGBA:
	case EGL_TEXTURE_EXTERNAL_WL:
		texture->has_alpha = true;
		break;
	default:
		tw_logl("invalid EGL buffer type, not supported");
		wlr_egl_destroy_image(rdr->egl, image);
		return false;
	}
	texture->image = image;
	texture->width = width;
	texture->height = height;
	texture->inverted_y = inverted_y;
	texture->wl_format = 0xFFFFFFFF;
	texture->target = GL_TEXTURE_EXTERNAL_OES;
	texture->rdr = rdr;

	TW_GLES_DEBUG_PUSH();

	glGenTextures(1, &texture->gltex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->gltex);
	rdr->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
	                                  texture->image);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

	assert(glGetError() == GL_NO_ERROR);

	TW_GLES_DEBUG_POP();

	wlr_egl_unset_current(rdr->egl);

	return true;
}

static void
dma_attributes_translate(struct wlr_dmabuf_attributes *dst,
                         struct tw_dmabuf_attributes *src)
{
	memset(dst, 0, sizeof(*dst));
	memset(dst->fd, -1, sizeof(dst->fd));
	dst->n_planes = src->n_planes;
	for (int i = 0; i < dst->n_planes; i++) {
		dst->fd[i] = src->fds[i];
		dst->offset[i] = src->offsets[i];
		dst->stride[i] = src->strides[i];
	}
	dst->flags = src->flags;
	dst->format = src->format;
	dst->modifier = src->modifier;
	dst->width = src->width;
	dst->height = src->height;
}

static bool
tw_render_texture_init_dma(struct tw_render_texture *texture,
                           struct tw_renderer *rdr,
                           struct tw_dmabuf_attributes *dma_attributes)
{
	struct wlr_dmabuf_attributes wlr_attrs;
	EGLImageKHR image;
	bool external_only = false;

	wlr_egl_make_current(rdr->egl, EGL_NO_SURFACE, NULL);

	if (!rdr->egl->exts.image_dmabuf_import_ext) {
		tw_logl("Cannot create DMA-buf texture without extensions.");
		return false;
	}
	dma_attributes_translate(&wlr_attrs, dma_attributes);

	switch(wlr_attrs.format & ~DRM_FORMAT_BIG_ENDIAN) {
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
	image = wlr_egl_create_image_from_dmabuf(rdr->egl, &wlr_attrs,
	                                         &external_only);
	if (image == EGL_NO_IMAGE_KHR) {
		tw_logl("failed to import the DMA-BUF image");
		return false;
	}
	texture->image = image;
	texture->width = wlr_attrs.width;
	texture->height = wlr_attrs.height;
	texture->wl_format = 0xFFFFFFFF;
	texture->inverted_y = (dma_attributes->flags &
	                       TW_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) != 0;
	texture->target = GL_TEXTURE_EXTERNAL_OES;
	texture->rdr = rdr;

	TW_GLES_DEBUG_PUSH();

	glGenTextures(1, &texture->gltex);
	glBindTexture(texture->target, texture->gltex);
	rdr->glEGLImageTargetTexture2DOES(texture->target,
	                                  texture->image);
	glBindTexture(texture->target, 0);

	assert(glGetError() == GL_NO_ERROR);

	TW_GLES_DEBUG_POP();

	wlr_egl_unset_current(rdr->egl);
	return true;
}

bool
tw_renderer_init_texture(struct tw_render_texture *texture,
                         struct tw_renderer *rdr, struct wl_resource *buffer)
{
	bool is_wl_drm_buffer, is_shm_buffer, is_dma_buffer;
	struct wl_shm_buffer *shmbuf;
	struct tw_dmabuf_buffer *dmabuf;

	is_shm_buffer = wl_shm_buffer_get(buffer) != NULL;
	is_wl_drm_buffer = wl_buffer_is_drm_texture(rdr, buffer);
	is_dma_buffer = tw_is_wl_buffer_dmabuf(buffer);

        if (!rdr->glEGLImageTargetTexture2DOES &&
            ( is_wl_drm_buffer || is_dma_buffer))
		return false;

        if (is_shm_buffer) {
	        shmbuf = wl_shm_buffer_get(buffer);
	        return tw_render_texture_init_pixels(texture, rdr, shmbuf);
        } else if (is_wl_drm_buffer) {
	        return tw_render_texture_init_wl_drm(texture, rdr, buffer);
        } else if (is_dma_buffer) {
	        dmabuf = tw_dmabuf_buffer_from_resource(buffer);
	        return tw_render_texture_init_dma(texture, rdr,
	                                          &dmabuf->attributes);
        } else  {
	        return false;
        }
}

struct tw_render_texture *
tw_renderer_new_texture(struct tw_renderer *rdr, struct wl_resource *buffer)
{
	struct tw_render_texture *texture;

	texture = calloc(1, sizeof(*texture));
	if (!texture)
		return NULL;
	if (!tw_renderer_init_texture(texture, rdr, buffer)) {
		free(texture);
		return NULL;
	}
	texture->destroy = tw_render_texture_free;
	return texture;
}

bool
tw_renderer_texture_update(struct tw_render_texture *texture,
                           struct tw_renderer *rdr,
                           struct wl_shm_buffer *buffer,
                           uint32_t src_x, uint32_t src_y,
                           uint32_t dst_x, uint32_t dst_y,
                           uint32_t width, uint32_t height)
{
	uint32_t stride;
	enum wl_shm_format format;
	GLuint glfmt;

	wlr_egl_make_current(rdr->egl, EGL_NO_SURFACE, NULL);
	format = wl_shm_buffer_get_format(buffer);
	stride = wl_shm_buffer_get_stride(buffer);
	if (!wl_format_supported(rdr, format))
		return false;
	glfmt = wl_format_to_gl_format(format);

	TW_GLES_DEBUG_PUSH();

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

	TW_GLES_DEBUG_POP();

	wlr_egl_unset_current(rdr->egl);
	return true;
}

bool
tw_renderer_import_dma(struct tw_render_texture *texture,
                       struct tw_renderer *rdr,
                       struct tw_dmabuf_attributes *dma_attributes)
{
	return tw_render_texture_init_dma(texture, rdr, dma_attributes);
}
