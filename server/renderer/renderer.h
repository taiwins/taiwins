/*
 * renderer.h - taiwins backend renderer header
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

#ifndef TW_RENDERER_H
#define TW_RENDERER_H

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include <taiwins/objects/surface.h>
#include <taiwins/objects/dmabuf.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend_output;

struct tw_renderer {
	struct wlr_egl *egl; /* our egl context. */
	struct wlr_renderer base;
	struct wlr_renderer_impl wlr_impl;
	struct wl_array pixel_formats; /* only records wl_shm_format */
	uint32_t viewport_w, viewport_h;
	struct {
		bool enable_debug;
	} options;

	struct {
		struct wl_signal pre_output_render;
		struct wl_signal post_ouptut_render;
		struct wl_signal pre_view_render;
		struct wl_signal post_view_render;
	} events;

	void (*repaint_output)(struct tw_renderer *renderer,
	                       struct tw_backend_output *output,
	                       int buffer_age);
	void (*notify_surface_destroy)(struct tw_renderer *renderer,
	                               struct tw_surface *surface);
	//TODO: reading pixels for recording.

	/** interfaces **/
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
};

struct tw_render_texture {
	uint32_t width, height;
	EGLint fmt;
	GLenum target; /**< GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
	bool has_alpha, inverted_y;
	enum wl_shm_format wl_format;
	GLuint gltex;
	EGLImageKHR image;

	struct tw_renderer *rdr;

	void (*destroy)(struct tw_render_texture *);
};

void
tw_gles_debug_push(const char *func);

void
tw_gles_debug_pop(void);

#define TW_GLES_DEBUG_PUSH() tw_gles_debug_push(__func__)
#define TW_GLES_DEBUG_POP() tw_gles_debug_pop()

bool
tw_renderer_import_dma(struct tw_render_texture *texture,
                       struct tw_renderer *rdr,
                       struct tw_dmabuf_attributes *dma_attributes);
bool
tw_renderer_texture_update(struct tw_render_texture *texture,
                           struct tw_renderer *rdr,
                           struct wl_shm_buffer *shmbuf,
                           uint32_t src_x, uint32_t src_y,
                           uint32_t dst_x, uint32_t dst_y,
                           uint32_t width, uint32_t height);
bool
tw_renderer_init_texture(struct tw_render_texture *texture,
                         struct tw_renderer *rdr, struct wl_resource *buffer);
struct tw_render_texture *
tw_renderer_new_texture(struct tw_renderer *rdr, struct wl_resource *buffer);

//how to texture reupload the dma and wl_drm textures?
void
tw_render_texture_destroy(struct tw_render_texture *texture);

bool
tw_renderer_init_base(struct tw_renderer *renderer,
                      struct wlr_egl *egl, EGLenum platform,
                      void *remote_display, EGLint visual_id);
void
tw_renderer_base_fini(struct tw_renderer *renderer);

struct wlr_renderer *
tw_layer_renderer_create(struct wlr_egl *egl, EGLenum platform,
                         void *remote_display, EGLint *config_attribs,
                         EGLint visual_id);

#ifdef  __cplusplus
}
#endif

#endif
