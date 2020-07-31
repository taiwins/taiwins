/*
 * shaders.h - taiwins server shaders
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

#ifndef TW_SHADERS_H
#define TW_SHADERS_H

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_quad_color_shader {
	GLuint prog;
	struct {
		GLint proj;
		GLint color;
	} uniform;
};

struct tw_quad_tex_shader {
	GLuint prog;
	/* used by normal alpha blending and gaussin blur shader */
	struct {
		GLint proj;
		GLint alpha;
		GLint texture;
	} uniform;
};

GLuint
tw_renderer_create_program(const GLchar *vs_src, const GLchar *fs_src);

void
tw_quad_color_shader_init(struct tw_quad_color_shader *shader);

void
tw_quad_color_shader_fini(struct tw_quad_color_shader *shader);

void
tw_quad_tex_blend_shader_init(struct tw_quad_tex_shader *shader);

void
tw_quad_tex_blend_shader_fini(struct tw_quad_tex_shader *shader);

void
tw_quad_tex_ext_blend_shader_init(struct tw_quad_tex_shader *shader);

void
tw_quad_tex_ext_blend_shader_fini(struct tw_quad_tex_shader *shader);

void
tw_quad_tex_blur_shader_init(struct tw_quad_tex_shader *shader);

void
tw_quad_tex_blur_shader_fini(struct tw_quad_tex_shader *shader);

#ifdef  __cplusplus
}
#endif

#endif
