/*
 * egl_shaders.c - taiwins egl renderer shaders
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
#include <GLES3/gl3.h>

#include <taiwins/objects/logger.h>
#include <taiwins/render_context_egl.h>
#include <wayland-util.h>

/******************************************************************************
 * shader collections
 *
 * GLES shaders works differently than GLSL, the gles program is called for
 * every surface. In taiwins case, A painter algorithm is used here to minimize
 * the draw cost. Depth buffer is not used here, but we maybe able to use
 * stencil buffer for discard the covered region.
 *
 * Vertex shader is simple and unified, projecting the a correct position, then
 * generting tex coordinates for fragment shader.
 *
 * Fragment shader is more interesting, as there is also a damaging system need
 * taking care of.
 *
 * We would like to reduce the bandwidth as much as possible.
 *****************************************************************************/

static const GLchar quad_vs[] =
	"uniform mat3 proj;\n"
	"uniform vec4 color;\n"
	"attribute vec2 pos;\n"
	"attribute vec2 texcoord;\n"
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
	"	o_color = color;\n"
	"	o_texcoord = texcoord;\n"
	"}\n";

static const GLchar color_quad_fs[] =
	"precision mediump float;\n"
	"uniform float alpha;\n"
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"\n"
	"void main() {\n"
	"	gl_FragColor = o_color * alpha;\n"
	"}\n"
	"\n";

static const GLchar tex_quad_fs[] =
	"precision mediump float;\n"
	"uniform float alpha;\n"
	"uniform sampler2D tex;\n"
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(tex, o_texcoord) * alpha;\n"
	"}\n"
	"\n";

const GLchar tex_quad_ext_fs[] =
	"#extension GL_OES_EGL_image_external : require\n\n"
	"precision mediump float;\n"
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"uniform samplerExternalOES tex;\n"
	"uniform float alpha;\n"
	"\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(tex, o_texcoord) * alpha;\n"
	"}\n";

static inline void
diagnose_shader(GLuint shader, GLenum type)
{
	GLint status, loglen;
	const char *type_str = type == GL_VERTEX_SHADER ?
		"vertex" : "fragment";

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &loglen);

	if (status != GL_TRUE) {
		char log_error[loglen+1];
		glGetShaderInfoLog(shader, loglen+1, &loglen, log_error);
		tw_logl("%s shader compile fails: %s\n",
		        type_str, log_error);
		glDeleteShader(shader);
	}
	assert(status == GL_TRUE);
}

static inline void
diagnose_program(GLuint prog)
{
	GLint st, loglen;
	glGetProgramiv(prog, GL_LINK_STATUS, &st);
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &loglen);

	if (st != GL_TRUE) {
		char log_error[loglen+1];
		glGetProgramInfoLog(prog, loglen+1, &loglen, log_error);
		tw_logl("program creation error: %s\n",
		        log_error);
		glDeleteProgram(prog);
	}
	assert(st == GL_TRUE);
}

static GLuint
compile_shader(GLenum type, const GLchar *src)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	diagnose_shader(shader, type);

	return shader;
}

/*****************************************************************************
 * exposed APIs
 ****************************************************************************/
WL_EXPORT GLuint
tw_egl_shader_create_program(const GLchar *vs_src, const GLchar *fs_src)
{
	GLuint p;
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

	p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	glDetachShader(p, vs);
	glDetachShader(p, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	diagnose_program(p);

	return p;
}

WL_EXPORT void
tw_egl_quad_color_shader_init(struct tw_egl_quad_shader *shader)
{
	shader->prog = tw_egl_shader_create_program(quad_vs, color_quad_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.target = glGetUniformLocation(shader->prog, "color");
	shader->uniform.alpha = glGetUniformLocation(shader->prog, "alpha");
	assert(shader->uniform.proj >= 0);
	assert(shader->uniform.target >= 0);
	assert(shader->uniform.alpha >= 0);
}

WL_EXPORT void
tw_egl_quad_color_shader_fini(struct tw_egl_quad_shader *shader)
{
	glDeleteProgram(shader->prog);
}

WL_EXPORT void
tw_egl_quad_tex_shader_init(struct tw_egl_quad_shader *shader)
{
	shader->prog = tw_egl_shader_create_program(quad_vs, tex_quad_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.target = glGetUniformLocation(shader->prog, "tex");
	shader->uniform.alpha = glGetUniformLocation(shader->prog, "alpha");
	assert(shader->uniform.proj >= 0);
	assert(shader->uniform.target >= 0);
	assert(shader->uniform.alpha >= 0);
}

WL_EXPORT void
tw_egl_quad_tex_shader_fini(struct tw_egl_quad_shader *shader)
{
	glDeleteProgram(shader->prog);
}

WL_EXPORT void
tw_egl_quad_texext_shader_init(struct tw_egl_quad_shader *shader)
{
	shader->prog = tw_egl_shader_create_program(quad_vs, tex_quad_ext_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.target = glGetUniformLocation(shader->prog, "tex");
	shader->uniform.alpha = glGetUniformLocation(shader->prog, "alpha");
	assert(shader->uniform.proj >= 0);
	assert(shader->uniform.target >= 0);
	assert(shader->uniform.alpha >= 0);
}

WL_EXPORT void
tw_egl_quad_texext_shader_fini(struct tw_egl_quad_shader *shader)
{
	glDeleteProgram(shader->prog);
}
