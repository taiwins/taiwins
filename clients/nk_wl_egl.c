#define NK_IMPLEMENTATION

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <stdbool.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
//pull in the nuklear headers so we can access eglapp
#include "nk_wl_egl.h"
#define NK_SHADER_VERSION "#version 330 core\n"


//define the globals
static const struct nk_draw_vertex_layout_element vertex_layout[] = {
	{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
	 NK_OFFSETOF(struct nk_egl_vertex, position)},
	{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
	 NK_OFFSETOF(struct nk_egl_vertex, uv)},
	{NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT,
	 NK_OFFSETOF(struct nk_egl_vertex, col)},
	{NK_VERTEX_LAYOUT_END}
};

static char cmd_buffer_data[4096];
/*
static struct nk_convert_config nk_config = {
	.vertex_layout = vertex_layout,
	.vertex_size = sizeof(struct egl_nk_vertex),
	.vertex_alignment = NK_ALIGNOF(struct egl_nk_vertex),
};
*/

NK_API void
nk_egl_font_stash_begin(struct nk_egl_backend *app, struct nk_font_atlas **atlas);
NK_API void
nk_egl_font_stash_end(struct nk_egl_backend *app);



static bool
nk_egl_compile_backend(struct nk_egl_backend *bkend)
{
	if (bkend->compiled)
		return true;
	GLint status, loglen;
	GLsizei stride;
	//////////////////// part 0) egl resource
	bkend->eglwin = wl_egl_window_create(bkend->wl_surface, 200, 200);
	assert(bkend->eglwin);
	bkend->eglsurface = eglCreateWindowSurface(bkend->env->egl_display, bkend->env->config,
						   (EGLNativeWindowType)bkend->eglwin, NULL);
	assert(bkend->eglsurface);
	assert(eglMakeCurrent(bkend->env->egl_display, bkend->eglsurface,
			      bkend->eglsurface, bkend->env->egl_context));
	//////////////////// part 1) OpenGL code
	static const GLchar *vertex_shader =
		NK_SHADER_VERSION
		"uniform mat4 ProjMtx;\n"
		"in vec2 Position;\n"
		"in vec2 TexCoord;\n"
		"in vec4 Color;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main() {\n"
		"   Frag_UV = TexCoord;\n"
		"   Frag_Color = Color;\n"
		"   gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
		"}\n";
	static const GLchar *fragment_shader =
		NK_SHADER_VERSION
		"precision mediump float;\n"
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"out vec4 Out_Color;\n"
		"void main(){\n"
		"   Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";
	bkend->glprog = glCreateProgram();
	bkend->vs = glCreateShader(GL_VERTEX_SHADER);
	bkend->fs = glCreateShader(GL_FRAGMENT_SHADER);
	assert(glGetError() == GL_NO_ERROR);
	glShaderSource(bkend->vs, 1, &vertex_shader, 0);
	glShaderSource(bkend->fs, 1, &fragment_shader, 0);
	glCompileShader(bkend->vs);
	glGetShaderiv(bkend->vs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(bkend->vs, GL_INFO_LOG_LENGTH, &loglen);
	assert(status == GL_TRUE);
	glCompileShader(bkend->fs);
	glGetShaderiv(bkend->fs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(bkend->fs, GL_INFO_LOG_LENGTH, &loglen);
	assert(status == GL_TRUE);
	glAttachShader(bkend->glprog, bkend->vs);
	glAttachShader(bkend->glprog, bkend->fs);
	glLinkProgram(bkend->glprog);
	glGetProgramiv(bkend->glprog, GL_LINK_STATUS, &status);
	assert(status == GL_TRUE);
	//locate the opengl resources
	glUseProgram(bkend->glprog);
	bkend->uniform_tex = glGetUniformLocation(bkend->glprog, "Texture");
	bkend->uniform_proj = glGetUniformLocation(bkend->glprog, "ProjMtx");
	bkend->attrib_pos = glGetAttribLocation(bkend->glprog, "Position");
	bkend->attrib_uv = glGetAttribLocation(bkend->glprog, "TexCoord");
	bkend->attrib_col = glGetAttribLocation(bkend->glprog, "Color");
	//assert
	assert(bkend->uniform_tex >= 0);
	assert(bkend->uniform_proj >= 0);
	assert(bkend->attrib_pos >= 0);
	assert(bkend->attrib_pos >= 0);
	assert(bkend->attrib_uv  >= 0);
	//assign the offsets
	stride = sizeof(struct nk_egl_vertex);
	off_t vp = offsetof(struct nk_egl_vertex, position);
	off_t vt = offsetof(struct nk_egl_vertex, uv);
	off_t vc = offsetof(struct nk_egl_vertex, col);

	glGenVertexArrays(1, &bkend->vao);
	glGenBuffers(1, &bkend->vbo);
	glGenBuffers(1, &bkend->ebo);
	glBindVertexArray(bkend->vao);
	glBindBuffer(GL_ARRAY_BUFFER, bkend->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bkend->ebo);
	assert(bkend->vao);
	assert(bkend->vbo);
	assert(bkend->ebo);
	//setup the offset
	glEnableVertexAttribArray(bkend->attrib_pos);
	glVertexAttribPointer(bkend->attrib_pos, 2, GL_FLOAT, GL_FALSE, stride, (void *)vp);
	glEnableVertexAttribArray(bkend->attrib_uv);
	glVertexAttribPointer(bkend->attrib_uv, 2, GL_FLOAT, GL_FALSE, stride, (void *)vt);
	glEnableVertexAttribArray(bkend->attrib_col);
	glVertexAttribPointer(bkend->attrib_col, 4, GL_FLOAT, GL_FALSE, stride, (void *)vc);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
	///////////////////////////////////
	//part 2) nuklear init
	struct nk_font_atlas *atlas;
	nk_init_default(&bkend->ctx, 0);
	nk_egl_font_stash_begin(&bkend, &atlas);
	nk_egl_font_stash_end(&bkend);
	nk_buffer_init_fixed(&bkend->cmds, cmd_buffer_data, sizeof(cmd_buffer_data));
	return true;
}

void
nk_egl_end_backend(struct nk_egl_backend *bkend)
{
	//opengl resource
	glDeleteBuffers(1, &bkend->vbo);
	glDeleteBuffers(1, &bkend->ebo);
	glDeleteVertexArrays(1, &bkend->vao);
	glDeleteTextures(1, &bkend->font_tex);
	glDeleteShader(bkend->vs);
	glDeleteShader(bkend->fs);
	glDeleteShader(bkend->glprog);
	//nuklear resource
	nk_font_atlas_cleanup(&bkend->atlas);
	nk_free(&bkend->ctx);
	//egl free context
	eglMakeCurrent(bkend->env->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(bkend->env->egl_display, bkend->eglsurface);
	wl_egl_window_destroy(bkend->eglwin);
	bkend->wl_surface = NULL;
	bkend->compiled = false;
}

void
nk_egl_render(struct nk_egl_backend *bkend)
{
	//map to gpu buffer
	struct nk_buffer vbuf, ebuf;
	GLfloat ortho[4][4] = {
		{ 2.0f,  0.0f,  0.0f, 0.0f},
		{ 0.0f, -2.0f,  0.0f, 0.0f},
		{ 0.0f,  0.0f, -1.0f, 0.0f},
		{-1.0f,  1.0f,  0.0f, 1.0f},
	};
	ortho[0][0] /= (GLfloat)bkend->width;
	ortho[1][1] /= (GLfloat)bkend->height;
	//setup the global state
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	glUseProgram(bkend->glprog);
	// I think it is really just font tex
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(bkend->uniform_tex, 0);
	glUniformMatrix4fv(bkend->uniform_proj, 1, GL_FALSE, &ortho[0][0]);


}


NK_INTERN void
nk_egl_upload_atlas(struct nk_egl_backend *app, const void *image, int width, int height)
{
	glGenTextures(1, &app->font_tex);
	glBindTexture(GL_TEXTURE_2D, app->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height,
		     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
}

NK_API void
nk_egl_font_stash_begin(struct nk_egl_backend *app, struct nk_font_atlas **atlas)
{
    nk_font_atlas_init_default(&app->atlas);
    nk_font_atlas_begin(&app->atlas);
    *atlas = &app->atlas;
}

NK_API void
nk_egl_font_stash_end(struct nk_egl_backend *app)
{
    const void *image; int w, h;
    image = nk_font_atlas_bake(&app->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_egl_upload_atlas(app, image, w, h);
    nk_font_atlas_end(&app->atlas, nk_handle_id((int)app->font_tex), &app->null);
    if (app->atlas.default_font) {
	    //we should have here though
	nk_style_set_font(&app->ctx, &app->atlas.default_font->handle);
    }
}
