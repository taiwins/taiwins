/*
 * render_context_egl.h - taiwins EGL render context
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

#ifndef TW_RENDER_CONTEXT_EGL_H
#define TW_RENDER_CONTEXT_EGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <wayland-server.h>
#include <taiwins/objects/egl.h>

#include "render_context.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_options;

struct tw_egl_quad_shader {
	GLuint prog;
	/* used by normal alpha blending and gaussin blur shader */
	struct {
		GLint proj; /* projection matrix */
		GLint alpha;
		GLint target; /**< used as color/texture */
	} uniform;
};

struct tw_egl_render_texture {
	struct tw_render_texture base;
	GLenum target;  /**< GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
	EGLImageKHR image;
	GLuint gltex;
};

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts);
GLuint
tw_egl_shader_create_program(const GLchar *vs_src, const GLchar *fs_src);

void
tw_egl_quad_color_shader_init(struct tw_egl_quad_shader *shader);

void
tw_egl_quad_color_shader_fini(struct tw_egl_quad_shader *shader);

void
tw_egl_quad_tex_shader_init(struct tw_egl_quad_shader *shader);

void
tw_egl_quad_tex_shader_fini(struct tw_egl_quad_shader *shader);

void
tw_egl_quad_texext_shader_init(struct tw_egl_quad_shader *shader);

void
tw_egl_quad_texext_shader_fini(struct tw_egl_quad_shader *shader);


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
