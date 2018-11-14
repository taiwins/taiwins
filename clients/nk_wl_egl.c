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
#include "client.h"
//for the configurations
#include "../config.h"
//pull in the nuklear headers so we can access eglapp

#define NK_IMPLEMENTATION
#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

#include "nk_wl_egl.h"
#define NK_SHADER_VERSION "#version 330 core\n"
#define NK_MAX_CTX_MEM 64 * 64 * 1024
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

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
	bool compiled;
	void *user_data;

	const struct egl_env *env;
	struct app_surface *app_surface;

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
	//attribute inside
	struct nk_context ctx;
	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_font_atlas atlas;
	//themes
	struct taiwins_theme theme;
	struct nk_colorf main_color;
	//free this
	nk_rune *unicode_range;

	unsigned char cmd_buffer[NK_EGL_CMD_SIZE];
	unsigned char ctx_buffer[NK_MAX_CTX_MEM];

	//up-to-date information
	nk_egl_draw_func_t frame;
	nk_egl_postcall_t post_cb;
	size_t width, height;
	struct nk_vec2 fb_scale;
	size_t row_size;

	struct {
		xkb_keysym_t ckey; //cleaned up every frame
		int32_t cbtn; //clean up every frame
		uint32_t sx;
		uint32_t sy;
	};
};

///////////////////////////////////////////////////////////////////
/////////////////////////// COLOR /////////////////////////////////
///////////////////////////////////////////////////////////////////

static void
nk_color_from_tw_rgba(struct nk_color *nc, const struct tw_rgba_t *tc)
{
	nc->r = tc->r; nc->g = tc->g;
	nc->b = tc->b; nc->a = tc->a;
}

static void
nk_colorf_from_tw_rgba(struct nk_colorf *nc, const struct tw_rgba_t *tc)
{
	nc->r = (float)tc->r/255.0; nc->g = (float)tc->g/255.0;
	nc->b = (float)tc->b/255.0; nc->a = (float)tc->a/255.0;
}

static void
nk_egl_apply_color(struct nk_egl_backend *bkend)
{
	if (bkend->theme.row_size == 0)
		return;
	//TODO this is a shitty hack, somehow the first draw call did not work, we
	//have to hack it in the background color
	nk_colorf_from_tw_rgba(&bkend->main_color, &bkend->theme.window_color);
	struct nk_color table[NK_COLOR_COUNT];
	nk_color_from_tw_rgba(&table[NK_COLOR_TEXT], &bkend->theme.text_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_WINDOW], &bkend->theme.window_color);
	//no header
	nk_color_from_tw_rgba(&table[NK_COLOR_HEADER], &bkend->theme.window_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_BORDER], &bkend->theme.border_color);
	//button
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON], &bkend->theme.button.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON_HOVER],
			      &bkend->theme.button.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON_ACTIVE],
			      &bkend->theme.button.active);
	//toggle
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE],
			      &bkend->theme.toggle.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE_HOVER],
			      &bkend->theme.toggle.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE_CURSOR],
			      &bkend->theme.toggle.active);
	//select
	nk_color_from_tw_rgba(&table[NK_COLOR_SELECT],
			      &bkend->theme.select.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_SELECT_ACTIVE],
			      &bkend->theme.select.active);
	//slider
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER],
			      &bkend->theme.slider_bg_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR],
			      &bkend->theme.slider.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR_HOVER],
			      &bkend->theme.slider.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR_ACTIVE],
			      &bkend->theme.slider.active);
	//property
	table[NK_COLOR_PROPERTY] = table[NK_COLOR_SLIDER];
	//edit
	nk_color_from_tw_rgba(&table[NK_COLOR_EDIT], &bkend->theme.text_active_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_EDIT_CURSOR], &bkend->theme.text_color);
	//combo
	nk_color_from_tw_rgba(&table[NK_COLOR_COMBO], &bkend->theme.combo_color);
	//chart
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART], &bkend->theme.chart.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART_COLOR], &bkend->theme.chart.active);
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART_COLOR_HIGHLIGHT],
			      &bkend->theme.chart.hover);
	//scrollbar
	table[NK_COLOR_SCROLLBAR] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_TAB_HEADER] = table[NK_COLOR_WINDOW];
	nk_style_from_table(&bkend->ctx, table);
}

///////////////////////////////////////////////////////////////////
//////////////////////////// FONT /////////////////////////////////
///////////////////////////////////////////////////////////////////

static inline void
union_unicode_range(const nk_rune left[2], const nk_rune right[2], nk_rune out[2])
{
	nk_rune tmp[2];
	tmp[0] = left[0] < right[0] ? left[0] : right[0];
	tmp[1] = left[1] > right[1] ? left[1] : right[1];
	out[0] = tmp[0];
	out[1] = tmp[1];
}

//return true if left and right are insersected, else false
static inline bool
intersect_unicode_range(const nk_rune left[2], const nk_rune right[2])
{
	return (left[0] <= right[1] && left[1] >= right[1]) ||
		(left[0] <= right[0] && left[1] >= right[0]);
}

static int
unicode_range_compare(const void *l, const void *r)
{
	const nk_rune *range_left = (const nk_rune *)l;
	const nk_rune *range_right = (const nk_rune *)r;
	return ((int)range_left[0] - (int)range_right[0]);
}

//we can only merge one range at a time
static int
merge_unicode_range(const nk_rune *left, const nk_rune *right, nk_rune *out)
{
	//get the range
	int left_size = 0;
	while(*(left+left_size)) left_size++;
	int right_size = 0;
	while(*(right+right_size)) right_size++;
	//sort the range,
	nk_rune sorted_ranges[left_size+right_size];
	memcpy(sorted_ranges, left, sizeof(nk_rune) * left_size);
	memcpy(sorted_ranges+left_size, right, sizeof(nk_rune) * right_size);
	qsort(sorted_ranges, (left_size+right_size)/2, sizeof(nk_rune) * 2,
	      unicode_range_compare);
	//merge algorithm
	nk_rune merged[left_size+right_size+1];
	merged[0] = sorted_ranges[0];
	merged[1] = sorted_ranges[1];
	int m = 0;
	for (int i = 0; i < (left_size+right_size) / 2; i++) {
		if (intersect_unicode_range(&sorted_ranges[i*2],
					    &merged[2*m]))
			union_unicode_range(&sorted_ranges[i*2], &merged[2*m],
					    &merged[2*m]);
		else {
			m++;
			merged[2*m] = sorted_ranges[2*i];
			merged[2*m+1] = sorted_ranges[2*i+1];
		}
	}
	m++;
	merged[2*m] = 0;

	if (!out)
		return 2*m;
	memcpy(out, merged, (2*m+1) * sizeof(nk_rune));
	return 2*m;
}

static struct nk_font*
nk_egl_prepare_font(struct nk_egl_backend *bkend)
{
	struct nk_font *font;
	int w, h;
	const void *image;
	struct nk_font_config cfg = nk_font_config(16);
	size_t len_range  = merge_unicode_range(nk_font_chinese_glyph_ranges(),
						nk_font_korean_glyph_ranges(),
						NULL);
	//we have to make this range available
	bkend->unicode_range = malloc(sizeof(nk_rune) * (len_range+1));
	merge_unicode_range(nk_font_chinese_glyph_ranges(),
			    nk_font_korean_glyph_ranges(), bkend->unicode_range);
	cfg.range = bkend->unicode_range;
	cfg.merge_mode = nk_false;
//	cfg.range = nk_font_chinese_glyph_ranges();

	nk_font_atlas_init_default(&bkend->atlas);
	nk_font_atlas_begin(&bkend->atlas);

	char *fonts[MAX_FONTS];
	size_t n_fonts = tw_theme_extract_fonts(&bkend->theme, fonts);
	//TODO we need font-config in
	if (n_fonts == 0) {
		/* font = nk_font_atlas_add_default(&bkend->atlas, 16.0, &cfg); */
		font = nk_font_atlas_add_from_file(&bkend->atlas,
						   "/usr/share/fonts/truetype/ttf-bitstream-vera/Vera.ttf",
					   /* "/usr/share/fonts/TTF/Vera.ttf", */
					   16, &cfg);
	} else {
		n_fonts = (n_fonts > 3) ? 3: n_fonts;
		for (int i = 0; i < n_fonts; i++) {
			font = nk_font_atlas_add_from_file(
				&bkend->atlas, fonts[i],
				tw_font_px2pt(bkend->row_size, 92), &cfg);
		}
	}
	image = nk_font_atlas_bake(&bkend->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
	//upload to the texture
	glGenTextures(1, &bkend->font_tex);
	glBindTexture(GL_TEXTURE_2D, bkend->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glBindTexture(GL_TEXTURE_2D, 0);
	nk_font_atlas_end(&bkend->atlas, nk_handle_id(bkend->font_tex), &bkend->null);
	//I should be able to free the image here?
	return font;
}

///////////////////////////////////////////////////////////////////
///////////////////////////// EGL /////////////////////////////////
///////////////////////////////////////////////////////////////////


static inline bool
is_surfless_supported(struct nk_egl_backend *bkend)
{
	const char *egl_extensions =  eglQueryString(bkend->env->egl_display, EGL_EXTENSIONS);
	//nvidia eglcontext does not bind to different surface with same context
	const char *egl_vendor = eglQueryString(bkend->env->egl_display, EGL_VENDOR);

	return (strstr(egl_extensions, "EGL_KHR_create_context") != NULL &&
		strstr(egl_extensions, "EGL_KHR_surfaceless_context") != NULL &&
		strstr(egl_vendor, "NVIDIA") == NULL);
}

static inline void
assign_egl_surface(struct app_surface *app_surface, const struct egl_env *env)
{
	assert(app_surface->eglsurface);
	//TODO on Nvidia driver, I am getting GL_INVALID_OPERATION on this, but
	//eglMakeCurrent succeed, a hack to make Nvidia happy
	assert(eglMakeCurrent(env->egl_display, app_surface->eglsurface,
			      app_surface->eglsurface, env->egl_context));
	glGetError();
	glViewport(0, 0, app_surface->w, app_surface->h);
	glScissor(0, 0, app_surface->w, app_surface->h);
}

static bool
compile_backend(struct nk_egl_backend *bkend, struct app_surface *app_surface)
{
	if (bkend->compiled)
		return true;

	GLint status, loglen;
	GLsizei stride;
	//part 0) testing the extension
	/* assign_egl_surface(app_surface, bkend->env); */
	if (is_surfless_supported(bkend)) {
		assert(eglMakeCurrent(bkend->env->egl_display,
				      EGL_NO_SURFACE, EGL_NO_SURFACE,
				      bkend->env->egl_context));
	} else
		assign_egl_surface(app_surface, bkend->env);

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
	//part 2) nuklear init
	//I guess it is probably easier to use the atlas
	struct nk_font *font = nk_egl_prepare_font(bkend);
	nk_init_fixed(&bkend->ctx, bkend->ctx_buffer, NK_MAX_CTX_MEM, &font->handle);
	nk_buffer_init_fixed(&bkend->cmds, bkend->cmd_buffer, sizeof(bkend->cmd_buffer));
	nk_buffer_clear(&bkend->cmds);
	nk_egl_apply_color(bkend);
	return true;
}


///////////////////////////////////////////////////////////////////
///////////////////////////// EGL /////////////////////////////////
///////////////////////////////////////////////////////////////////

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
	glValidateProgram(bkend->glprog);
	glClearColor(bkend->main_color.r, bkend->main_color.g,
		     bkend->main_color.b, bkend->main_color.a);
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
	static char nk_last_cmds[64 * 4096] = {0};
	void *cmds = nk_buffer_memory(&bkend->ctx.memory);
	bool need_redraw = memcmp(cmds, nk_last_cmds, bkend->ctx.memory.allocated);
	if (!need_redraw) {
		nk_clear(&bkend->ctx);
		return;
	}
	memcpy(nk_last_cmds, cmds, bkend->ctx.memory.allocated);

	const struct nk_draw_command *cmd;
	nk_draw_index *offset = NULL;
	struct nk_buffer vbuf, ebuf;
	_nk_egl_draw_begin(bkend, &vbuf, &ebuf);
	//TODO MESA driver has a problem, the first draw call did not work, we can
	//avoid it by draw a quad that does nothing
	nk_draw_foreach(cmd, &bkend->ctx, &bkend->cmds) {
		if (!cmd->elem_count)
			continue;
		glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
		GLint scissor_region[4] = {
			(GLint)(cmd->clip_rect.x * bkend->fb_scale.x),
			(GLint)((bkend->height - (cmd->clip_rect.y + cmd->clip_rect.h)) *
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
	eglSwapBuffers(bkend->env->egl_display,
		       bkend->app_surface->eglsurface);
}

static void
nk_egl_new_frame(struct nk_egl_backend *bkend)
{
	if (nk_begin(&bkend->ctx, "eglapp", nk_rect(0, 0, bkend->width, bkend->height),
		     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
		bkend->frame(&bkend->ctx, bkend->width, bkend->height, bkend->user_data);
	} nk_end(&bkend->ctx);

	nk_egl_render(bkend);
	//call the post_cb if any
	if (bkend->post_cb) {
		bkend->post_cb(bkend->user_data);
		bkend->post_cb = NULL;
	}

	bkend->ckey = XKB_KEY_NoSymbol;
	bkend->cbtn = -1;
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
	}
	else if (modifier & TW_ALT) {
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_LEFT, (keysym == XKB_KEY_b) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_RIGHT, (keysym == XKB_KEY_f) && state);
	}
	//no tabs, we don't essentially need a buffer here, give your own buffer. That is it.
	else if (keycode >= 0x20 && keycode < 0x7E && state)
		nk_input_unicode(&bkend->ctx, keycode);
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
	//we can actually just trigger the rendering if we have a symbol.
	nk_egl_new_frame(bkend);
}

static void
nk_pointron(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	nk_input_begin(&bkend->ctx);
	nk_input_motion(&bkend->ctx, sx, sy);
	nk_input_end(&bkend->ctx);
	bkend->sx = sx;
	bkend->sy = sy;

	nk_egl_new_frame(bkend);
}

static void
nk_pointrbtn(struct app_surface *surf, enum taiwins_btn_t btn, bool state, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	enum nk_buttons b;
	switch (btn) {
	case TWBTN_LEFT:
		b = NK_BUTTON_LEFT;
		break;
	case TWBTN_RIGHT:
		b = NK_BUTTON_RIGHT;
		break;
	case TWBTN_MID:
		b = NK_BUTTON_MIDDLE;
		break;
	case TWBTN_DCLICK:
		b = NK_BUTTON_DOUBLE;
		break;
	}

	nk_input_begin(&bkend->ctx);
	nk_input_button(&bkend->ctx, b, (int)sx, (int)sy, state);
	nk_input_end(&bkend->ctx);

	bkend->cbtn = (state) ? b : -1;
	bkend->sx = sx;
	bkend->sy = sy;

	nk_egl_new_frame(bkend);
}

static void
nk_pointraxis(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy)
{
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)surf->parent;
	nk_input_begin(&bkend->ctx);
	nk_input_scroll(&bkend->ctx, nk_vec2(direction * (float)sx, (direction * (float)sy)));
	nk_input_begin(&bkend->ctx);
	nk_egl_new_frame(bkend);
}



static void
release_backend(struct nk_egl_backend *bkend)
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

		nk_buffer_free(&bkend->cmds);
		free(bkend->unicode_range);
		//egl free context
		eglMakeCurrent(bkend->env->egl_display, NULL, NULL, NULL);
		bkend->compiled = false;
		bkend->width = 0;
		bkend->height = 0;
		bkend->fb_scale = nk_vec2(1.0, 1.0);
	}
}

struct nk_egl_instance {
	struct nk_egl_backend *bkend;
	nk_egl_draw_func_t draw_cb;
};


/********************* exposed APIS *************************/
void nk_egl_swap_proc(struct app_surface *surf, uint32_t user_data)
{
	//TODO impl this
}


void nk_egl_impl_app_surface(struct app_surface *surf,
			     struct nk_egl_backend *bkend,
			     nk_egl_draw_func_t draw_func,
			     uint32_t w, uint32_t h,
			     uint32_t px, uint32_t py)
{
	struct nk_egl_instance *instance = malloc(sizeof(struct nk_egl_instance));
	instance->bkend = bkend;
	instance->draw_cb = draw_func;

	surf->w = w;
	surf->h = h;
	surf->px = px;
	surf->py = py;
	//assume it is compiled
	app_surface_init_egl(surf, (struct egl_env *)bkend->env);
	if (surf->wl_globals) {
		//TODO, include the theme in the wl_globals, so we can set it here.
	}
	assign_egl_surface(surf, bkend->env);
}



struct nk_egl_backend*
nk_egl_create_backend(const struct egl_env *env)
{
	//we probably should uses
	struct nk_egl_backend *bkend = (struct nk_egl_backend *)calloc(1, sizeof(*bkend));
	bkend->env = env;
	bkend->app_surface = NULL;
	bkend->compiled = false;

	return bkend;
}

void
nk_egl_launch(struct nk_egl_backend *bkend,
	      struct app_surface *app_surface,
	      nk_egl_draw_func_t func,
	      void *data)
{
	if (bkend->app_surface)
		app_surface_release(bkend->app_surface);

	{
		const unsigned int w = app_surface->w;
		const unsigned int h = app_surface->h;
		const unsigned int s = app_surface->s;
		bkend->width = w;
		bkend->height = h;
		bkend->fb_scale = nk_vec2(s, s);
		appsurface_init_input(app_surface, nk_keycb, nk_pointron, nk_pointrbtn, nk_pointraxis);
		app_surface->parent = (struct app_surface *)bkend;
		bkend->app_surface = app_surface;
		bkend->cbtn = -1;
		bkend->ckey = XKB_KEY_NoSymbol;
	}

	bkend->frame = func;
	bkend->user_data = data;

	//now resize the window
	bkend->compiled = compile_backend(bkend, app_surface);
	assign_egl_surface(app_surface, bkend->env);

	nk_egl_new_frame(bkend);
	//there seems to be no function about changing window size in egl
}

void
nk_egl_update(struct nk_egl_backend *bkend)
{
	nk_egl_new_frame(bkend);
}

void
nk_egl_close(struct nk_egl_backend *bkend, struct app_surface *app_surface)
{
	if (!is_surfless_supported(bkend))
		release_backend(bkend);
	app_surface_release(app_surface);
	//reset the egl_surface
	bkend->app_surface = NULL;
	eglMakeCurrent(bkend->env->egl_display, NULL, NULL, NULL);
}


void
nk_egl_destroy_backend(struct nk_egl_backend *bkend)
{
	release_backend(bkend);
	free(bkend);
}

bool
nk_egl_set_theme(struct nk_egl_backend *bkend, const struct taiwins_theme *theme)
{
	bkend->theme = *theme;
	//validate there
	return true;
}


xkb_keysym_t
nk_egl_get_keyinput(struct nk_context *ctx)
{
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	return bkend->ckey;
}

bool
nk_egl_get_btn(struct nk_context *ctx, enum nk_buttons *button, uint32_t *sx, uint32_t *sy)
{
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	*button = (bkend->cbtn >= 0) ? bkend->cbtn : NK_BUTTON_MAX;
	*sx = bkend->sx;
	*sy = bkend->sy;
	return (bkend->cbtn) >= 0;
}


void
nk_egl_add_idle(struct nk_context *ctx,
		void (*task)(void *user_data))
{
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	bkend->post_cb = task;
}


#ifdef __DEBUG

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


void nk_egl_capture_framebuffer(struct nk_context *ctx, const char *path)
{
	EGLint gl_pack_alignment;
	//okay, I can use glreadpixels, so I don't need additional framebuffer
	struct nk_egl_backend *bkend = container_of(ctx, struct nk_egl_backend, ctx);
	//create rgba8 data
	unsigned char *data = malloc(bkend->width * bkend->height * 4);
	cairo_surface_t *s = cairo_image_surface_create_for_data(
		data, CAIRO_FORMAT_ARGB32,
		bkend->width, bkend->height,
		cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, bkend->width));

	glGetIntegerv(GL_PACK_ALIGNMENT, &gl_pack_alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, bkend->width, bkend->height,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_pack_alignment);

	//now flip the image
	cairo_surface_t *s1 = cairo_image_surface_create(
		CAIRO_FORMAT_RGB24, bkend->width, bkend->height);
	cairo_t *cr = cairo_create(s1);
	cairo_matrix_t matrix;
	cairo_matrix_init(&matrix,
			  1, 0, 0, -1, 0, bkend->height);
	cairo_transform(cr, &matrix);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_paint(cr);
	cairo_surface_write_to_png(s1, path);
	cairo_destroy(cr);
	cairo_surface_destroy(s1);
	cairo_surface_destroy(s);
	free(data);
}


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

/* void */
/* nk_egl_debug_draw_command(struct nk_egl_backend *bkend) */
/* { */
/*	const struct nk_draw_command *cmd; */
/*	size_t stride = sizeof(struct nk_egl_vertex); */

/*	nk_draw_foreach(cmd, &bkend->ctx, &bkend->cmds) { */

/*	} */
/* } */

#endif
