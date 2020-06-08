/*
 * shaders.c - taiwins backend renderer shaders
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

#include "shaders.h"
#include "../../taiwins.h"

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
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"\n"
	"void main() {\n"
	"	gl_FragColor = o_color;\n"
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

static const GLchar tex_gaussian_fs[] =
	"precision mediump float;\n"
	"uniform float alpha;\n"
	"uniform sampler2D tex;\n"
	"uniform vec2 texsize;\n"
	"varying vec4 o_color;\n"
	"varying vec2 o_texcoord;\n"
	"\n"
	"float gaussian_kernel(int x, int y) {\n"
	"	mat3 kernel = mat3(\n"
	"			1.0/16.0, 1.0/8.0, 1.0/16.0,\n"
	"			1.0/8.0, 1.0/4.0, 1.0/8.0,\n"
	"			1.0/16.0, 1.0/8.0, 1.0/16.0);\n"
	"	return kernel[x+1][y+1];\n"
	"}\n"
	"\n"
	"void main() {\n"
	"	vec2 step = vec2(1.0/texsize.x, 1.0/texsize.y);\n"
	"	vec4 color = vec4(0.0f);\n"
	"	for (int i = -1; i <= 1; i++) {\n"
	"		for (int j = -1; j <= 1; j++) {\n"
	"			vec2 coord = vec2(o_texcoord.x+i*step.x, \n"
	"			                  o_texcoord.y+j*step.y);\n"
	"			color += gaussian_kernel(i,j) * \n"
	"				texture2D(tex, coord);\n"
	"		}\n"
	"	}\n"
	"	gl_FragColor = color * alpha;\n"
	"}\n"
	"\n";

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

static GLuint
create_program(const GLchar *vs_src, const GLchar *fs_src)
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

/*****************************************************************************
 * exposed APIs
 ****************************************************************************/

void
tw_quad_color_shader_init(struct tw_quad_color_shader *shader)
{
	shader->prog = create_program(quad_vs, color_quad_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.color = glGetUniformLocation(shader->prog, "color");
	assert(shader->uniform.proj);
	assert(shader->uniform.color);
}

void
tw_quad_color_shader_fini(struct tw_quad_color_shader *shader)
{
	glDeleteProgram(shader->prog);
}

void
tw_quad_tex_blend_shader_init(struct tw_quad_tex_shader *shader)
{
	shader->prog = create_program(quad_vs, tex_quad_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.texture = glGetUniformLocation(shader->prog, "tex");
	shader->uniform.alpha = glGetUniformLocation(shader->prog, "alpha");
	assert(shader->uniform.proj);
	assert(shader->uniform.texture);
	assert(shader->uniform.alpha);
}

void
tw_quad_tex_blend_shader_fini(struct tw_quad_tex_shader *shader)
{
	glDeleteProgram(shader->prog);
}

void
tw_quad_tex_blur_shader_init(struct tw_quad_tex_shader *shader)
{
	shader->prog = create_program(quad_vs, tex_gaussian_fs);
	shader->uniform.proj = glGetUniformLocation(shader->prog, "proj");
	shader->uniform.texture = glGetUniformLocation(shader->prog, "tex");
	shader->uniform.alpha = glGetUniformLocation(shader->prog, "alpha");
	assert(shader->uniform.proj);
	assert(shader->uniform.texture);
	assert(shader->uniform.alpha);
}

void
tw_quad_tex_blur_shader_fini(struct tw_quad_tex_shader *shader)
{
	glDeleteProgram(shader->prog);
}
