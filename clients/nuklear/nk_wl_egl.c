#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/input.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <time.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
#include <wayland-client.h>

#define NK_IMPLEMENTATION
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

#define NK_SHADER_VERSION "#version 330 core\n"

#include "../client.h"
#include "../egl.h"
#include "nk_wl_internal.h"
#include <helpers.h>

//vao layout
//I could probably use more compat format, and we need float32_t
struct nk_egl_vertex {
	float position[2];
	float uv[2];
	nk_byte col[4];
};

//define the globals
static const struct nk_draw_vertex_layout_element vertex_layout[] = {
	{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
	 NK_OFFSETOF(struct nk_egl_vertex, position)},
	{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
	 NK_OFFSETOF(struct nk_egl_vertex, uv)},
	{NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
	 NK_OFFSETOF(struct nk_egl_vertex, col)},
	{NK_VERTEX_LAYOUT_END}
};

/* as we known that we are using the fixed memory here, the size is crucial to
 * our needs, if the size is too small, we will run into `nk_mem_alloc`
 * failed. If we are giving too much memory, it is then a certain waste. 16Mb is
 * the sweat spot that most widgets will fit */

/**
 * @brief nk_egl_backend
 *
 * nuklear EGL backend for wayland, this backend uses EGL and OpenGL 3.3 for
 * rendering the widgets. The rendering loop is a bit different than typical
 * window toolkit like GLFW/SDL.
 *
 * GFLW window toolkit traps everything in a loop. the loops blocks at the
 * system events, then process the events(intput, time for updating
 * normal), then uses the up-to-date information for rendering.
 *
 * In the our case, such loop doesn't exists(yet, we may add the loop support in
 * the future to support FPS control, but it will not be here. GLFW can do more
 * optimization, it can accumlate the events and do just one pass of rendering
 * for multiple events).
 *
 * In our case, the events triggers a frame for the rendering(no rendering if
 * there is no update). Then nuklear updates the context then OpenGL does the
 * rendering.
 */
struct nk_egl_backend {
	struct nk_wl_backend base;
	//OpenGL resource
	struct {
		struct egl_env env;
		bool compiled;
		GLuint glprog, vs, fs;
		GLuint vao, vbo, ebo;
		GLuint font_tex;
		GLint attrib_pos;
		GLint attrib_uv;
		GLint attrib_col;
		//uniforms
		GLint uniform_tex;
		GLint uniform_proj;
	};
	//nuklear resources
	struct nk_buffer cmds;	//cmd to opengl vertices
	struct nk_font_atlas atlas;
	struct nk_draw_null_texture null;
	size_t font_size;

	unsigned char cmd_buffer[NK_EGL_CMD_SIZE];
};


///////////////////////////////////////////////////////////////////
//////////////////////////// FONT /////////////////////////////////
///////////////////////////////////////////////////////////////////

static const nk_rune basic_range[] = {0x0020, 0x00ff, 0};
static const nk_rune pua_range[] = {0xE000, 0xF8FF, 0,};

static void
nk_egl_release_font(struct nk_egl_backend *bkend)
{
	if (bkend->font_tex) {
		glDeleteTextures(1, &bkend->font_tex);
		nk_font_atlas_clear(&bkend->atlas);
		bkend->font_tex = 0;
		/* free(bkend->base.unicode_range); */
		bkend->base.unicode_range = NULL;
	}
}

static struct nk_font*
nk_egl_prepare_font(struct nk_egl_backend *bkend, size_t font_size)
{
	nk_egl_release_font(bkend);
	bkend->font_size = font_size;

	//right now we need to hard code the font files
	const char *vera = "/usr/share/fonts/TTF/Vera.ttf";
	const char *fa_a = "/usr/share/fonts/TTF/fa-solid-900.ttf";

	struct nk_font *font;
	int w, h;
	const void *image;
	struct nk_font_config cfg = nk_font_config(font_size);

	size_t len_range  = merge_unicode_range(nk_font_default_glyph_ranges(),
						pua_range, NULL);

	nk_font_atlas_init_default(&bkend->atlas);
	nk_font_atlas_begin(&bkend->atlas);
	cfg.range = basic_range;
	cfg.merge_mode = nk_false;
	font = nk_font_atlas_add_from_file(&bkend->atlas, vera, font_size, &cfg);

	cfg.merge_mode = nk_true;
	cfg.range = pua_range;
	nk_font_atlas_add_from_file(&bkend->atlas, fa_a, font_size, &cfg);
	//why do we need rgba32, because you need to support image too, maybe
	//you need a differetn shader?
	image = nk_font_atlas_bake(&bkend->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
	//upload to the texture
	glGenTextures(1, &bkend->font_tex);
	glBindTexture(GL_TEXTURE_2D, bkend->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glBindTexture(GL_TEXTURE_2D, 0);
	//now call this so the image is freed
	nk_font_atlas_end(&bkend->atlas, nk_handle_id(bkend->font_tex), &bkend->null);
	nk_font_atlas_cleanup(&bkend->atlas);
	//I should be able to free the image here?
	return font;
}

///////////////////////////////////////////////////////////////////
///////////////////////////// EGL /////////////////////////////////
///////////////////////////////////////////////////////////////////

static inline bool
is_surfless_supported(struct nk_egl_backend *bkend)
{
	const char *egl_extensions =  eglQueryString(bkend->env.egl_display, EGL_EXTENSIONS);
	//nvidia eglcontext does not bind to different surface with same context
	const char *egl_vendor = eglQueryString(bkend->env.egl_display, EGL_VENDOR);

	return (strstr(egl_extensions, "EGL_KHR_create_context") != NULL &&
		strstr(egl_extensions, "EGL_KHR_surfaceless_context") != NULL &&
		strstr(egl_vendor, "NVIDIA") == NULL);
}

static inline void
assign_egl_surface(EGLSurface eglsurface, const struct egl_env *env)
{
	assert(eglsurface);
	//TODO on Nvidia driver, I am getting GL_INVALID_OPERATION on this, but
	//eglMakeCurrent succeed, a hack to make Nvidia happy
	EGLint egl_error = eglGetError();
	/* printf("egl error: %x\n", egl_error); */
	assert(eglMakeCurrent(env->egl_display, eglsurface,
			      eglsurface, env->egl_context));
	egl_error = eglGetError();
	(void)egl_error;
	/* printf("egl error: %x\n", egl_error); */
	/* glViewport(0, 0, app_surface->w, app_surface->h); */
	/* glScissor(0, 0, app_surface->w, app_surface->h); */
}

static bool
compile_backend(struct nk_egl_backend *bkend, EGLSurface eglsurface)
{
	if (bkend->compiled)
		return true;

	GLint status, loglen;
	GLsizei stride;
	struct egl_env *env = &bkend->env;
	//part 0) testing the extension
	/* assign_egl_surface(app_surface, bkend->env); */
	//maybe I should try this on new driver
	if (is_surfless_supported(bkend)) {
		assert(eglMakeCurrent(env->egl_display,
				      EGL_NO_SURFACE, EGL_NO_SURFACE,
				      env->egl_context));
	} else
		assign_egl_surface(eglsurface, env);

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
	glBufferData(GL_ARRAY_BUFFER, MAX_VERTEX_BUFFER, NULL, GL_STREAM_DRAW);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_ELEMENT_BUFFER, NULL, GL_STREAM_DRAW);

	assert(bkend->vao);
	assert(bkend->vbo);
	assert(bkend->ebo);
	//setup the offset
	glEnableVertexAttribArray(bkend->attrib_pos);
	glVertexAttribPointer(bkend->attrib_pos, 2, GL_FLOAT, GL_FALSE, stride, (void *)vp);
	glEnableVertexAttribArray(bkend->attrib_uv);
	glVertexAttribPointer(bkend->attrib_uv, 2, GL_FLOAT, GL_FALSE, stride, (void *)vt);
	glEnableVertexAttribArray(bkend->attrib_col);
	glVertexAttribPointer(bkend->attrib_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)vc);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
	///////////////////////////////////
	return true;
}

static void
release_backend(struct nk_egl_backend *bkend)
{
	//release the font
	nk_egl_release_font(bkend);
	if (bkend->compiled) {
		//opengl resource
		glDeleteBuffers(1, &bkend->vbo);
		glDeleteBuffers(1, &bkend->ebo);
		glDeleteVertexArrays(1, &bkend->vao);
		glDeleteShader(bkend->vs);
		glDeleteShader(bkend->fs);
		glDeleteShader(bkend->glprog);
		//nuklear resource
		//egl free context
		eglMakeCurrent(bkend->env.egl_display, NULL, NULL, NULL);
		bkend->compiled = false;
	}
}

///////////////////////////////////////////////////////////////////
/////////////////////////// render ////////////////////////////////
///////////////////////////////////////////////////////////////////

static void
_nk_egl_draw_begin(struct nk_egl_backend *bkend,
		   struct nk_buffer *vbuf, struct nk_buffer *ebuf,
		   int width, int height)
{
	void *vertices = NULL;
	void *elements = NULL;
	//NOTE update uniform
	GLfloat ortho[4][4] = {
		{ 2.0f,  0.0f,  0.0f, 0.0f},
		{ 0.0f, -2.0f,  0.0f, 0.0f},
		{ 0.0f,  0.0f, -1.0f, 0.0f},
		{-1.0f,  1.0f,  0.0f, 1.0f},
	};
	ortho[0][0] /= (GLfloat)width;
	ortho[1][1] /= (GLfloat)height;
	//use program
	glUseProgram(bkend->glprog);
	glValidateProgram(bkend->glprog);
	glClearColor(bkend->base.main_color.r, bkend->base.main_color.g,
		     bkend->base.main_color.b, bkend->base.main_color.a);
	glClear(GL_COLOR_BUFFER_BIT);
	//switches
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	//uniforms
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(bkend->uniform_tex, 0);
	glUniformMatrix4fv(bkend->uniform_proj, 1, GL_FALSE, &ortho[0][0]);
	//vertex buffers
	//it could be actually a bottle neck
	glBindVertexArray(bkend->vao);
	glBindBuffer(GL_ARRAY_BUFFER, bkend->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bkend->ebo);
	assert(glGetError() == GL_NO_ERROR);
	//I guess it is not really a good idea to allocate buffer every frame.
	//if we already have the glBufferData, we would just mapbuffer
	vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	elements = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
	{
		struct nk_convert_config config;
		nk_memset(&config, 0, sizeof(config));
		config.vertex_layout = vertex_layout;
		config.vertex_size = sizeof(struct nk_egl_vertex);
		config.vertex_alignment = NK_ALIGNOF(struct nk_egl_vertex);
		config.null = bkend->null;
		config.circle_segment_count = 2;;
		config.curve_segment_count = 22;
		config.arc_segment_count = 2;;
		config.global_alpha = 1.0f;
		config.shape_AA = NK_ANTI_ALIASING_ON;
		config.line_AA = NK_ANTI_ALIASING_ON;
		nk_buffer_init_fixed(vbuf, vertices, MAX_VERTEX_BUFFER);
		nk_buffer_init_fixed(ebuf, elements, MAX_ELEMENT_BUFFER);
		nk_convert(&bkend->base.ctx, &bkend->cmds, vbuf, ebuf, &config);
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
}

static void
_nk_egl_draw_end(struct nk_egl_backend *bkend)
{
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	//do I need this?
	nk_buffer_clear(&bkend->cmds);
}

static void
nk_wl_render(struct nk_wl_backend *b)
{
	struct nk_egl_backend *bkend = container_of(b, struct nk_egl_backend, base);
	struct egl_env *env = &bkend->env;
	struct app_surface *app = b->app_surface;
	struct nk_context *ctx = &b->ctx;
	if (!nk_wl_need_redraw(b))
		return;
	//make current to current
	eglMakeCurrent(env->egl_display, app->eglsurface,
		       app->eglsurface, env->egl_context);

	const struct nk_draw_command *cmd;
	nk_draw_index *offset = NULL;
	struct nk_buffer vbuf, ebuf;
	//be awere of this.
	int width = b->app_surface->w;
	int height = b->app_surface->h;
	int scale  = b->app_surface->s;

	_nk_egl_draw_begin(bkend, &vbuf, &ebuf, width, height);
	//TODO MESA driver has a problem, the first draw call did not work, we can
	//avoid it by draw a quad that does nothing
	nk_draw_foreach(cmd, ctx, &bkend->cmds) {
		if (!cmd->elem_count)
			continue;
		glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
		GLint scissor_region[4] = {
			(GLint)(cmd->clip_rect.x * scale),
			(GLint)((height - (cmd->clip_rect.y + cmd->clip_rect.h)) *
				scale),
			(GLint)(cmd->clip_rect.w * scale),
			(GLint)(cmd->clip_rect.h * scale),
		};
		glScissor(scissor_region[0], scissor_region[1],
			  scissor_region[2], scissor_region[3]);
		glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count,
			       GL_UNSIGNED_SHORT, offset);
		offset += cmd->elem_count;
	}

	_nk_egl_draw_end(bkend);
	eglSwapBuffers(env->egl_display,
		       app->eglsurface);
}

static void
nk_egl_destroy_app_surface(struct app_surface *app)
{
	struct nk_wl_backend *b = app->user_data;
	struct nk_egl_backend *bkend = container_of(b, struct nk_egl_backend, base);
	if (!is_surfless_supported(bkend))
		release_backend(bkend);
	app_surface_clean_egl(app, &bkend->env);
	nk_wl_clean_app_surface(b);
}


/********************* exposed APIS *************************/

/* this function is expected to be called every time when you want to open a surface.
 * the backend is occupied entirely by this app_surface through his lifetime */
void
nk_egl_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			nk_wl_drawcall_t draw_cb,
			uint32_t w, uint32_t h, uint32_t x, uint32_t y)

{
	struct nk_egl_backend *b = container_of(bkend, struct nk_egl_backend, base);
	nk_wl_impl_app_surface(surf, bkend, draw_cb, w, h, x, y);
	surf->destroy = nk_egl_destroy_app_surface;
	//assume it is compiled
	app_surface_init_egl(surf, &b->env);
	//font is not initialized here
	b->compiled = compile_backend(b, surf->eglsurface);
	if (b->font_size != bkend->row_size) {
		//prepare new font
		b->font_size = bkend->row_size;
		struct nk_font *font = nk_egl_prepare_font(b, bkend->row_size);
		nk_style_set_font(&bkend->ctx, &font->handle);
	}

	assign_egl_surface(surf->eglsurface, &b->env);
}

struct nk_wl_backend*
nk_egl_create_backend(const struct wl_display *display, const struct egl_env *shared_env)
{
	//we do not have any font here,
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)calloc(1, sizeof(*bkend));
	if (shared_env)
		egl_env_init_shared(&bkend->env, shared_env);
	else
		egl_env_init(&bkend->env, display);
	bkend->font_size = 0;
	bkend->font_tex = 0;

	bkend->compiled = false;
	//part 2) nuklear init, font is initialized later
	nk_init_fixed(&bkend->base.ctx, bkend->base.ctx_buffer, NK_MAX_CTX_MEM, NULL);
	nk_buffer_init_fixed(&bkend->cmds, bkend->cmd_buffer, sizeof(bkend->cmd_buffer));
	nk_buffer_clear(&bkend->cmds);

	return &bkend->base;
}

void
nk_egl_destroy_backend(struct nk_wl_backend *b)
{
	struct nk_egl_backend *bkend =
		container_of(b, struct nk_egl_backend, base);
	release_backend(bkend);
	egl_env_end(&bkend->env);
	nk_free(&bkend->base.ctx);
	nk_buffer_free(&bkend->cmds);
	free(bkend);

}

//this need to include in a c file and we include it from there


#ifdef __DEBUG
/*
void
nk_egl_resize(struct nk_egl_backend *bkend, int32_t width, int32_t height)
{
	struct app_surface *app_surface = bkend->app_surface;
	app_surface->w = width;
	app_surface->h = height;
	wl_egl_window_resize(app_surface->eglwin, width, height, 0, 0);
	glViewport(0, 0, width, height);
	glScissor(0, 0, width, height);
}
*/
/*
void nk_egl_capture_framebuffer(struct nk_context *ctx, const char *path)
{
	EGLint gl_pack_alignment;
	//okay, I can use glreadpixels, so I don't need additional framebuffer
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	int width = bkend->app_surface->w;
	int height = bkend->app_surface->h;
	int scale = bkend->app_surface->s;

	//create rgba8 data
	unsigned char *data = malloc(width * height * 4);
	cairo_surface_t *s = cairo_image_surface_create_for_data(
		data, CAIRO_FORMAT_ARGB32,
		width, height,
		cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width));

	glGetIntegerv(GL_PACK_ALIGNMENT, &gl_pack_alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_pack_alignment);

	//now flip the image
	cairo_surface_t *s1 = cairo_image_surface_create(
		CAIRO_FORMAT_RGB24, width, height);
	cairo_t *cr = cairo_create(s1);
	cairo_matrix_t matrix;
	cairo_matrix_init(&matrix,
			  1, 0, 0, -1, 0, height);
	cairo_transform(cr, &matrix);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_paint(cr);
	cairo_surface_write_to_png(s1, path);
	cairo_destroy(cr);
	cairo_surface_destroy(s1);
	cairo_surface_destroy(s);
	free(data);
}
*/
/*
void
nk_egl_debug_command(struct nk_egl_backend *bkend)
{
	const char *command_type[] = {
		"NK_COMMAND_NOP",
		"NK_COMMAND_SCISSOR",
		"NK_COMMAND_LINE",
		"NK_COMMAND_CURVE",
		"NK_COMMAND_RECT",
		"NK_COMMAND_RECT_FILLED",
		"NK_COMMAND_RECT_MULTI_COLOR",
		"NK_COMMAND_CIRCLE",
		"NK_COMMAND_CIRCLE_FILLED",
		"NK_COMMAND_ARC",
		"NK_COMMAND_ARC_FILLED",
		"NK_COMMAND_TRIANGLE",
		"NK_COMMAND_TRIANGLE_FILLED",
		"NK_COMMAND_POLYGON",
		"NK_COMMAND_POLYGON_FILLED",
		"NK_COMMAND_POLYLINE",
		"NK_COMMAND_TEXT",
		"NK_COMMAND_IMAGE",
		"NK_COMMAND_CUSTOM",
	};
	const struct nk_command *cmd = 0;
	int idx = 0;
	nk_foreach(cmd, &bkend->ctx) {
		fprintf(stderr, "%d command: %s \t", idx++, command_type[cmd->type]);
	}
	fprintf(stderr, "\n");
}
*/

/* void */
/* nk_egl_debug_draw_command(struct nk_egl_backend *bkend) */
/* { */
/*	const struct nk_draw_command *cmd; */
/*	size_t stride = sizeof(struct nk_egl_vertex); */

/*	nk_draw_foreach(cmd, &bkend->ctx, &bkend->cmds) { */

/*	} */
/* } */

#endif
