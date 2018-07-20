#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <stdbool.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
#include <wayland-client.h>
#include "client.h"
//pull in the nuklear headers so we can access eglapp

#define NK_IMPLEMENTATION
#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

#include "nk_wl_egl.h"
#define NK_SHADER_VERSION "#version 330 core\n"
#define NK_MAX_CTX_MEM 16 * 1024 * 1024
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

//vao layout
struct nk_egl_vertex {
	float position[2];
	float uv[2];
	float col[4];
};


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
/*we are giving 4Kb mem to the command buffer*/
static char cmd_buffer_data[4096];
/* and 16Mb to the context. */
static char *nk_ctx_buffer = NULL;
/* as we known that we are using the fixed memory here, the size is crucial to
 * our needs, if the size is too small, we will run into `nk_mem_alloc`
 * failed. If we are giving too much memory, it is then a certain waste. 16Mb is
 * the sweat spot that most widgets will fit */



//I dont think we can really do this, you need to somehow implement the
//wl_input_callbacks, and the implmentation is not here. So the solution must be
//implementing the raw wayland callbacks...
struct nk_egl_backend {
	bool compiled;
	const struct egl_env *env;
	struct app_surface *app_surface;
	struct wl_surface *wl_surface;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;

	//opengl resources
	GLuint glprog, vs, fs;//actually, we can evider vs, fs
	GLuint vao, vbo, ebo;
	GLuint font_tex;
	GLint attrib_pos;
	GLint attrib_uv;
	GLint attrib_col;
	//uniforms
	//this texture is used for font and pictures though
	GLint uniform_tex;
	GLint uniform_proj;
	//as we have the atlas font, we need to have the texture
	//if we have the attrib here, we can actually define the vertex
	//attribute inside
	struct nk_context ctx;
	//ctx has all the information, vertex info, we are not supposed to bake it
	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_font_atlas atlas;

	//up-to-date information
	size_t width, height;
	struct nk_vec2 fb_scale;
	nk_egl_draw_func_t frame;
	xkb_keysym_t ckey;
	void *user_data;
};

/*
static struct nk_convert_config nk_config = {
	.vertex_layout = vertex_layout,
	.vertex_size = sizeof(struct egl_nk_vertex),
	.vertex_alignment = NK_ALIGNOF(struct egl_nk_vertex),
};
*/


/*********** static implementations *********/
static struct nk_font*
nk_egl_prepare_font(struct nk_egl_backend *bkend)
{
	struct nk_font *font;
	int w, h;
	const void *image;
	struct nk_font_config cfg = nk_font_config(16);

	nk_font_atlas_init_default(&bkend->atlas);
	nk_font_atlas_begin(&bkend->atlas);
	//now we are using default font, latter we will switch to font-config to
	font = nk_font_atlas_add_default(&bkend->atlas, 16.0, &cfg);
	image = nk_font_atlas_bake(&bkend->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
	//upload to the texture
	glGenTextures(1, &bkend->font_tex);
	glBindTexture(GL_TEXTURE_2D, bkend->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glBindTexture(GL_TEXTURE_2D, 0);
	nk_font_atlas_end(&bkend->atlas, nk_handle_id(bkend->font_tex), &bkend->null);
	return font;
}

static bool
_compile_backend(struct nk_egl_backend *bkend)
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
	glVertexAttribPointer(bkend->attrib_col, 4, GL_FLOAT, GL_FALSE, stride, (void *)vc);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
	///////////////////////////////////
	//part 2) nuklear init
	//I guess it is probably easier to use the atlas
	nk_ctx_buffer = calloc(1, NK_MAX_CTX_MEM);
	struct nk_font *font = nk_egl_prepare_font(bkend);
	nk_init_fixed(&bkend->ctx, nk_ctx_buffer, NK_MAX_CTX_MEM, &font->handle);
	nk_buffer_init_fixed(&bkend->cmds, cmd_buffer_data, sizeof(cmd_buffer_data));
	nk_buffer_clear(&bkend->cmds);
	return true;
}


static void
_nk_egl_draw_begin(struct nk_egl_backend *bkend, struct nk_buffer *vbuf, struct nk_buffer *ebuf)
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
	ortho[0][0] /= (GLfloat)bkend->width;
	ortho[1][1] /= (GLfloat)bkend->height;
	//use program
	glUseProgram(bkend->glprog);
	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
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
		nk_convert(&bkend->ctx, &bkend->cmds, vbuf, ebuf, &config);
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

	nk_buffer_clear(&bkend->cmds);
}

static void
nk_egl_render(struct nk_egl_backend *bkend)
{
	//convert the command queue
	const struct nk_draw_command *cmd;
	nk_draw_index *offset = NULL;
	struct nk_buffer vbuf, ebuf;
	//we should check the command buffer first, if nothing changes we should
	//just return
	//however, this doesn't work
	/* void *mem = nk_buffer_memory(&bkend->ctx.memory); */
	/* if (!memcmp(mem, bkend->last_cmds, bkend->ctx.memory.allocated)) */
	/*	return; */
	/* else */
	/*	memcpy(bkend->last_cmds, mem, bkend->ctx.memory.allocated); */
	_nk_egl_draw_begin(bkend, &vbuf, &ebuf);
	nk_draw_foreach(cmd, &bkend->ctx, &bkend->cmds) {
		if (!cmd->elem_count)
			continue;
		glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
		GLint scissor_region[4] = {
			(GLint)(cmd->clip_rect.x * bkend->fb_scale.x),
			(GLint)((bkend->height -
				 (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) *
				bkend->fb_scale.y),
			(GLint)(cmd->clip_rect.w * bkend->fb_scale.x),
			(GLint)(cmd->clip_rect.h * bkend->fb_scale.y),
		};
		glScissor(scissor_region[0], scissor_region[1],
			  scissor_region[2], scissor_region[3]);
		glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count,
			       GL_UNSIGNED_SHORT, offset);
		offset += cmd->elem_count;
	}
	nk_clear(&bkend->ctx);
	_nk_egl_draw_end(bkend);
	eglSwapBuffers(bkend->env->egl_display, bkend->eglsurface);
}


static void
_nk_egl_new_frame(struct nk_egl_backend *bkend)
{
	if (nk_begin(&bkend->ctx, "eglapp", nk_rect(0, 0, bkend->width, bkend->height),
		     NK_WINDOW_BORDER)) {
		bkend->frame(&bkend->ctx, bkend->width, bkend->height, bkend->user_data);
	} nk_end(&bkend->ctx);
	nk_egl_render(bkend);
}


static void
nk_keycb(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier, int state)
{
	//nk_input_key and nk_input_unicode are different, you kinda need to
	//registered all the keys
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	uint32_t keycode = xkb_keysym_to_utf32(keysym);
	nk_input_begin(&bkend->ctx);
	//now we deal with the ctrl-keys
	if (modifier & TW_CTRL) {
		//the emacs keybindings
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_START, (keysym == XKB_KEY_a) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_END, (keysym == XKB_KEY_e) && state);
		nk_input_key(&bkend->ctx, NK_KEY_LEFT, (keysym == XKB_KEY_b) && state);
		nk_input_key(&bkend->ctx, NK_KEY_RIGHT, (keysym == XKB_KEY_f) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_UNDO, (keysym == XKB_KEY_slash) && state);
		//we should also support the clipboard later
//		nk_input_key(&bkend->ctx, NK_KEY_COPY, )
	} else if (modifier & TW_ALT) {
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_LEFT, (keysym == XKB_KEY_b) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_RIGHT, (keysym == XKB_KEY_f) && state);
	}
	//no tabs, we don't essentially need a buffer here, give your own buffer. That is it.
	else if (keycode >= 0x20 && keycode < 0x7E && state)
		nk_input_unicode(&bkend->ctx, keycode);
//		bkend->text_len++;
	else {
		nk_input_key(&bkend->ctx, NK_KEY_DEL, (keysym == XKB_KEY_Delete) && state);
		nk_input_key(&bkend->ctx, NK_KEY_ENTER, (keysym == XKB_KEY_Return) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TAB, keysym == XKB_KEY_Tab && state);
		nk_input_key(&bkend->ctx, NK_KEY_BACKSPACE, (keysym == XKB_KEY_BackSpace) && state);
		nk_input_key(&bkend->ctx, NK_KEY_UP, (keysym == XKB_KEY_UP) && state);
		nk_input_key(&bkend->ctx, NK_KEY_DOWN, (keysym == XKB_KEY_DOWN) && state);
		nk_input_key(&bkend->ctx, NK_KEY_SHIFT, (keysym == XKB_KEY_Shift_L ||
							 keysym == XKB_KEY_Shift_R) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_START, (keysym == XKB_KEY_Home) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_END, (keysym == XKB_KEY_End) && state);
		nk_input_key(&bkend->ctx, NK_KEY_LEFT, (keysym == XKB_KEY_Left) && state);
		nk_input_key(&bkend->ctx, NK_KEY_RIGHT, (keysym == XKB_KEY_Right) && state);
	}
//	fprintf(stderr, "we have the modifier %d\n", modifier);
	if (state)
		bkend->ckey = keysym;
	else
		bkend->ckey = XKB_KEY_NoSymbol;
	nk_input_end(&bkend->ctx);
	_nk_egl_new_frame(bkend);
}

static void
nk_pointron(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	nk_input_begin(&bkend->ctx);
	nk_input_motion(&bkend->ctx, sx, sy);
	nk_input_end(&bkend->ctx);
	_nk_egl_new_frame(bkend);
}

static void
nk_pointrbtn(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	nk_input_begin(&bkend->ctx);
	nk_input_button(&bkend->ctx, (btn) ? NK_BUTTON_LEFT : NK_BUTTON_RIGHT, (int)sx, (int)sy, 1);
	nk_input_end(&bkend->ctx);
	_nk_egl_new_frame(bkend);

}

static void
nk_pointraxis(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	nk_input_begin(&bkend->ctx);
	nk_input_scroll(&bkend->ctx, nk_vec2(direction * (float)sx, (direction * (float)sy)));
	nk_input_begin(&bkend->ctx);
	_nk_egl_new_frame(bkend);
}



/********************* exposed APIS *************************/

struct nk_egl_backend*
nk_egl_create_backend(const struct egl_env *env, struct wl_surface *attached_to)
{
	//we probably should uses
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)calloc(1, sizeof(*bkend));
	bkend->env = env;
	bkend->wl_surface = attached_to;
	bkend->app_surface = app_surface_from_wl_surface(attached_to);
	//tmp pointer casting, if we could have better solution
	bkend->app_surface->parent = NULL;
	bkend->compiled = false;
	appsurface_init_input(bkend->app_surface, nk_keycb, nk_pointron, nk_pointrbtn, nk_pointraxis);
	return bkend;
}

void
nk_egl_launch(struct nk_egl_backend *bkend, int w, int h, float s,
	      nk_egl_draw_func_t func,
	      void *data)
{
	bkend->width = w;
	bkend->height = h;
	bkend->fb_scale = nk_vec2(s, s);
	bkend->frame = func;
	bkend->user_data = data;
	//now resize the window
	bkend->compiled = _compile_backend(bkend);
	wl_egl_window_resize(bkend->eglwin, w, h, 0, 0);
	_nk_egl_new_frame(bkend);
	//there seems to be no function about changing window size in egl
}


void
nk_egl_destroy_backend(struct nk_egl_backend *bkend)
{
	if (bkend->compiled) {
		//opengl resource
		glDeleteBuffers(1, &bkend->vbo);
		glDeleteBuffers(1, &bkend->ebo);
		glDeleteVertexArrays(1, &bkend->vao);
		glDeleteTextures(1, &bkend->font_tex);
		glDeleteShader(bkend->vs);
		glDeleteShader(bkend->fs);
		glDeleteShader(bkend->glprog);
		//nuklear resource
		nk_font_atlas_clear(&bkend->atlas);
		nk_free(&bkend->ctx);
		//use the clear, cleanup is used for creating second font

		free(nk_ctx_buffer);
		nk_ctx_buffer = NULL;
		nk_buffer_free(&bkend->cmds);
		//egl free context
		eglMakeCurrent(bkend->env->egl_display, NULL, NULL, NULL);
		eglDestroySurface(bkend->env->egl_display, bkend->eglsurface);
		/* eglMakeCurrent(bkend->env->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); */

		wl_egl_window_destroy(bkend->eglwin);
	}
	bkend->compiled = false;
	//
	free(bkend);
}


xkb_keysym_t
nk_egl_get_keyinput(struct nk_context *ctx)
{
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	return bkend->ckey;
}
