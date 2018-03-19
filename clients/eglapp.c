#define NK_IMPLEMENTATION
#include <time.h>
#include <assert.h>

#ifdef _WITH_NVIDIA
#include <eglexternalplatform.h>
#endif

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>


#include <GL/gl.h>
#include <GL/glext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#include "egl.h"
#include "shellui.h"
#include "client.h"
#include <wayland-taiwins-shell-client-protocol.h>



/*
 * ===============================================================
 *
 *                 EGL application book-keeping
 *
 * ===============================================================
 */


struct app_surface *
appsurface_from_icon(struct eglapp_icon *icon)
{
	struct eglapp *app = container_of(icon, struct eglapp, icon);
	return &app->surface;
}

struct eglapp *
eglapp_from_icon(struct eglapp_icon *i)
{
	return container_of(i, struct eglapp, icon);
}

struct eglapp_icon *
icon_from_eglapp(struct eglapp *app)
{
	return &app->icon;
}
struct app_surface *
appsurface_from_app(struct eglapp *app)
{
	return &app->surface;
}



static void
eglapp_key_cb(struct app_surface *surf, xkb_keysym_t keysym);

static void
eglapp_cursor_motion_cb(struct app_surface *surf, uint32_t sx, uint32_t sy);
static void
eglapp_cursor_button_cb(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy);

static void
eglapp_cursor_axis_cb(struct app_surface *surf, int speed, int direction, uint32_t sx, uint32_t sy);


void
eglapp_init_with_funcs(struct eglapp *app,
		       void (*update_icon)(struct eglapp_icon *),
		       void (*draw_widget)(struct eglapp *))
{
	struct eglapp_icon *icon = &app->icon;
	struct app_surface *appsurf = &app->surface;
	//get around with the
	update_icon(&app->icon);
	unsigned int icon_width = cairo_image_surface_get_width(icon->isurf);
	unsigned int icon_right = icon->box.w;
	icon->box = (struct bbox) {
		.x = icon_right - icon_width,
		.y = 0,
		.w = icon_width,
		.h = icon->box.h,
	};
	//the anchor point of the app
	app->surface.px = icon->box.x + icon->box.w / 2;
	app->surface.py = 0;

	app->width = 50;
	app->width = 50;
	//callbacks
	app->icon.update_icon = update_icon;
	app->draw_widget = draw_widget;
	//TODO, temp code again, you need to remove this
	//setup callbacks
	app->surface.keycb = eglapp_key_cb;
	app->surface.pointron = eglapp_cursor_motion_cb;
	app->surface.pointrbtn = eglapp_cursor_button_cb;
	app->surface.pointraxis = eglapp_cursor_axis_cb;
	app->surface.paint_subsurface = NULL;
}

void
eglapp_init_with_script(struct eglapp *app,
	const char *script)
{
	int status = 0;

	app->L = luaL_newstate();
	luaL_openlibs(app->L);
	status += luaL_loadfile(app->L, script);
	status += lua_pcall(app->L, 0, 0, 0);
	if (status)
		return;

	//register globals
	void *ptr = lua_newuserdata(app->L, sizeof(void *));
	*(struct eglapp **)ptr = app;
	lua_setglobal(app->L, "application");
	//setup callbacks
	app->surface.keycb = eglapp_key_cb;
	app->surface.pointron = eglapp_cursor_motion_cb;
	app->surface.pointrbtn = eglapp_cursor_button_cb;
	app->surface.pointraxis = eglapp_cursor_axis_cb;
}

void
eglapp_decide_location(struct eglapp *app)
{

}

void
eglapp_destroy(struct eglapp *app)
{
	cairo_destroy(app->icon.ctxt);
	cairo_surface_destroy(app->icon.isurf);
}

static void
_free_eglapp(void *app)
{
	eglapp_destroy((struct eglapp *)app);
}

struct eglapp*
eglapp_addtolist(struct app_surface *panel, vector_t *widgets)
{
	struct bbox box;
	struct eglapp *lastapp, *newapp;
	struct app_surface *eglsurf;

	if (!widgets->elems)
		vector_init(widgets, sizeof(struct eglapp), _free_eglapp);
	lastapp = (struct eglapp *)vector_at(widgets, widgets->len-1);
	//decide where to put the icon
	if (!lastapp) {
		box = (struct bbox) {
			.x=0, .y=0,
			.w = panel->w,
			.h = panel->h
		};
	} else {
		box = (struct bbox) {
			.x = 0, .y = 0,
			.w = lastapp->surface.px,
			.h= panel->h
		};
	}
	newapp = (struct eglapp *)vector_newelem(widgets);
	memset(newapp, 0, sizeof(*newapp));
	//setup appsurf
	eglsurf = &newapp->surface;
	eglsurf->parent = panel;
	newapp->surface.wl_output = panel->wl_output;
	//we do the same thing, now we have place holders
	newapp->icon.box = box;

	return newapp;
}

void
eglapp_dispose(struct eglapp *app)
{
	glDeleteBuffers(1, &app->vbo);
	glDeleteBuffers(1, &app->ebo);
	glDeleteVertexArrays(1, &app->vao);
	glDeleteTextures(1, &app->font_tex);
	glDeleteShader(app->vs);
	glDeleteShader(app->fs);
	glDeleteProgram(app->glprog);
	cairo_destroy(app->icon.ctxt);
	cairo_surface_destroy(app->icon.isurf);
}

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifndef NK_EGLAPP_DOUBLE_CLICK_LO
#define NK_EGLAPP_DOUBLE_CLICK_LO 0.02
#endif

#ifndef NK_EGLAPP_DOUBLE_CLICK_HI
#define NK_EGLAPP_DOUBLE_CLICK_HI 0.2
#endif

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024


void
eglapp_key_cb(struct app_surface *surf, xkb_keysym_t keysym)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
	//maybe you will need to call the render here as well
//	nk_input_begin(ctx);
	//now we need a key translation library...
	nk_input_key(ctx, NK_KEY_LEFT, true);
//	nk_input_end(ctx);
}

void
eglapp_cursor_motion_cb(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
//	nk_input_begin(ctx);
	//now we need a key translation library...
	nk_input_motion(ctx, sx, sy);
//	nk_input_end(ctx);
}

void
eglapp_cursor_button_cb(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
	nk_input_begin(ctx);
	nk_input_button(ctx, (btn) ? NK_BUTTON_LEFT : NK_BUTTON_RIGHT, sx, sy, true);
	nk_input_end(ctx);
}

void
eglapp_cursor_axis_cb(struct app_surface *surf, int speed, int direction, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
	nk_input_begin(ctx);
	nk_input_scroll(ctx, nk_vec2(speed * direction, speed *(1-direction)));
	nk_input_end(ctx);
}



NK_API void
nk_egl_char_callback(struct eglapp *win, unsigned int codepoint);
NK_API void
nk_eglapp_new_frame(struct eglapp *app);
NK_API void
nk_eglapp_render(struct eglapp *app, enum nk_anti_aliasing AA, int max_vertex_buffer,
		 int max_element_buffer);

NK_API void nk_eglapp_font_stash_begin(struct eglapp *app, struct nk_font_atlas **atlas);
NK_API void nk_eglapp_font_stash_end(struct eglapp *app);




#define NK_SHADER_VERSION "#version 330 core\n"
struct egl_nk_vertex {
	float position[2];
	float uv[2];
	nk_byte col[4];
};

static void eglapp_find_launch_point(struct eglapp *app, int *x, int *y)
{
	struct app_surface *panel = app->surface.parent;
	//rightmost
	if (app->surface.px + app->surface.w / 2 > panel->w)
		*x = panel->w - app->surface.w;
	else
		*x = (int)(app->surface.px - app->surface.w/2) < 0 ?
			0 : app->surface.px - app->surface.w;
	*y = panel-> h + 10;
}

//okay, I can only create program after creating a window
void
eglapp_launch(struct eglapp *app, struct egl_env *env, struct wl_compositor *compositor)
{
	GLint status, loglen;
	app->eglenv = env;

	app->surface.wl_surface = wl_compositor_create_surface(compositor);
	//wl_egl_window_create was implemented
	app->eglwin = wl_egl_window_create(app->surface.wl_surface, 100, 100);
	assert(app->eglwin);
	int x, y;
	eglapp_find_launch_point(app, &x, &y);
	//I should create the view here
	taiwins_shell_set_widget(shelloftaiwins, app->surface.wl_surface, app->surface.wl_output, x, y);
	app->eglsurface = eglCreateWindowSurface(env->egl_display, env->config,
						 (EGLNativeWindowType)app->eglwin, NULL);
	assert(app->eglsurface);
	if (!eglMakeCurrent(env->egl_display, app->eglsurface, app->eglsurface, env->egl_context)) {
		fprintf(stderr, "failed to launch the window\n");
	}
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
	app->glprog = glCreateProgram();
	app->vs = glCreateShader(GL_VERTEX_SHADER);
	app->fs = glCreateShader(GL_FRAGMENT_SHADER);
	/* fprintf(stderr, "the gl program with id %u %u %u\n", */
	/*	app->glprog, app->vs, app->fs); */
	assert(glGetError() == GL_NO_ERROR);
	glShaderSource(app->vs, 1, &vertex_shader, 0);
	glShaderSource(app->fs, 1, &fragment_shader, 0);
	glCompileShader(app->vs);
	glCompileShader(app->fs);
	glGetShaderiv(app->vs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(app->vs, GL_INFO_LOG_LENGTH, &loglen);
	if (status != GL_TRUE) {
		char err_msg[loglen];
		glGetShaderInfoLog(app->vs, loglen, NULL, err_msg);
		fprintf(stderr, "vertex shader compile fails: %s\n", err_msg);
	}
	assert(status == GL_TRUE);
	glGetShaderiv(app->fs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(app->fs, GL_INFO_LOG_LENGTH, &loglen);
	/* if (status != GL_TRUE) { */
	/*	char err_msg[loglen]; */
	/*	glGetShaderInfoLog(app->fs, loglen, NULL, err_msg); */
	/*	fprintf(stderr, "fragment shader compile fails: %s\n", err_msg); */
	/* } */
	assert(status == GL_TRUE);
	//link shader into program
	glAttachShader(app->glprog, app->vs);
	glAttachShader(app->glprog, app->fs);
	glLinkProgram(app->glprog);
	glGetProgramiv(app->glprog, GL_LINK_STATUS, &status);
	assert(status == GL_TRUE);
	//creating uniforms
	app->uniform_tex = glGetUniformLocation(app->glprog, "Texture");
	app->uniform_proj = glGetUniformLocation(app->glprog, "ProjMtx");
	app->attrib_pos = glGetAttribLocation(app->glprog, "Position");
	app->attrib_uv = glGetAttribLocation(app->glprog, "TexCoord");
	app->attrib_col = glGetAttribLocation(app->glprog, "Color");
	assert(app->attrib_pos >= 0);
	assert(app->attrib_pos >= 0);
	assert(app->attrib_uv  >= 0);
	{
		//setup vab, vbo
		GLsizei vs = sizeof(struct egl_nk_vertex);
		size_t vp = offsetof(struct egl_nk_vertex, position);
		size_t vt = offsetof(struct egl_nk_vertex, uv);
		size_t vc = offsetof(struct egl_nk_vertex, col);

		glGenBuffers(1, &app->vbo);
		glGenBuffers(1, &app->ebo);
		glGenVertexArrays(1, &app->vao);

		glBindVertexArray(app->vao);
		glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ebo);

		glVertexAttribPointer(app->attrib_pos, 2, GL_FLOAT, GL_FALSE,
				      vs, (void *)vp);
		glVertexAttribPointer(app->attrib_uv, 2, GL_FLOAT, GL_FALSE,
				      vs, (void *)vt);
		glVertexAttribPointer(app->attrib_col, 4, GL_FLOAT, GL_FALSE,
				      vs, (void *)vc);
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	//call the frame for the first time
	{
		struct nk_font_atlas *atlas;
		nk_eglapp_font_stash_begin(app, &atlas);
		nk_eglapp_font_stash_end(app);
	}
	nk_eglapp_new_frame(app);
}

NK_INTERN void
nk_eglapp_upload_atlas(struct eglapp *app, const void *image, int width, int height)
{
	glGenTextures(1, &app->font_tex);
	glBindTexture(GL_TEXTURE_2D, app->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height,
		     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
}

NK_API void
nk_eglapp_font_stash_begin(struct eglapp *app, struct nk_font_atlas **atlas)
{
    nk_font_atlas_init_default(&app->atlas);
    nk_font_atlas_begin(&app->atlas);
    *atlas = &app->atlas;
}

NK_API void
nk_eglapp_font_stash_end(struct eglapp *app)
{
    const void *image; int w, h;
    image = nk_font_atlas_bake(&app->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_eglapp_upload_atlas(app, image, w, h);
    nk_font_atlas_end(&app->atlas, nk_handle_id((int)app->font_tex), &app->null);
    if (app->atlas.default_font)
	nk_style_set_font(&app->ctx, &app->atlas.default_font->handle);
}



NK_API void
nk_eglapp_render(struct eglapp *app, enum nk_anti_aliasing AA, int max_vertex_buffer,
	int max_element_buffer)
{
	struct nk_buffer vbuf, ebuf;
	GLfloat ortho[4][4] = {
		{ 2.0f,  0.0f,  0.0f, 0.0f},
		{ 0.0f, -2.0f,  0.0f, 0.0f},
		{ 0.0f,  0.0f, -1.0f, 0.0f},
		{-1.0f,  1.0f,  0.0f, 1.0f},
	};
	ortho[0][0] /= (GLfloat)app->width;
	ortho[1][1] /= (GLfloat)app->height;

	//setup the global state
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glActiveTexture(GL_TEXTURE0);

	glUseProgram(app->glprog);
	glUniform1i(app->uniform_tex, 0);
	glUniformMatrix4fv(app->uniform_proj, 1, GL_FALSE, &ortho[0][0]);
	//about this, we need to test
	glViewport(0, 0, app->width, app->height);
	{
		//convert the command queue
		const struct nk_draw_command *cmd;
		void *vertices = NULL;
		void *elements = NULL;
		const nk_draw_index *offset = NULL;

		glBindVertexArray(app->vao);
		glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ebo);

		glBufferData(GL_ARRAY_BUFFER, max_vertex_buffer,
			     NULL, GL_STREAM_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, max_element_buffer,
			     NULL, GL_STREAM_DRAW);
		{
		//convert
		struct nk_convert_config config;
		static const struct nk_draw_vertex_layout_element vertex_layout[] = {
			{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
			 NK_OFFSETOF(struct egl_nk_vertex, position)},
			{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
			 NK_OFFSETOF(struct egl_nk_vertex, uv)},
			{NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
			 NK_OFFSETOF(struct egl_nk_vertex, col)},
			{NK_VERTEX_LAYOUT_END}
		};
		nk_memset(&config, 0, sizeof(config));
		config.vertex_layout = vertex_layout;
		config.vertex_size = sizeof(struct egl_nk_vertex);
		config.vertex_alignment = NK_ALIGNOF(struct egl_nk_vertex);
		config.null = app->null;
		config.circle_segment_count = 2;;
		config.curve_segment_count = 22;
		config.arc_segment_count = 2;;
		config.global_alpha = 1.0f;
		config.shape_AA = AA;
		config.line_AA = AA;

		nk_buffer_init_fixed(&vbuf, vertices, (size_t)max_vertex_buffer);
		nk_buffer_init_fixed(&ebuf, elements, (size_t)max_element_buffer);
		nk_convert(&app->ctx, &app->cmds, &vbuf, &ebuf, &config);
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

		nk_draw_foreach(cmd, &app->ctx, &app->cmds) {
			if (!cmd->elem_count)
				continue;
			glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
			glScissor(
				(GLint)(cmd->clip_rect.x * app->fb_scale.x),
				(GLint)((app->height -
					 (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) *
					app->fb_scale.y),
				(GLint)(cmd->clip_rect.w * app->fb_scale.x),
				(GLint)(cmd->clip_rect.h * app->fb_scale.y));
			glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count,
				       GL_UNSIGNED_SHORT, offset);
			offset += cmd->elem_count;
		}
		nk_clear(&app->ctx);
	}
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
}


NK_API void
nk_egl_char_callback(struct eglapp *win, unsigned int codepoint)
{
    (void)win;
    if (win->text_len < NK_EGLAPP_TEXT_MAX)
	win->text[win->text_len++] = codepoint;
}



NK_API void
nk_eglapp_new_frame(struct eglapp *app)
{
	struct nk_context *ctx = &app->ctx;

	nk_input_begin(ctx);
	for (int i = 0; i < app->text_len; i++)
		nk_input_unicode(ctx, app->text[i]);
	nk_input_end(ctx);
	if (nk_begin(ctx, "eglapp", nk_rect(0,0, app->width, app->height),
		     NK_WINDOW_BORDER)) {
		//TODO, change the draw function to app->draw_widget(app);
		enum {EASY, HARD};
		nk_layout_row_static(ctx, 30, 80, 1);
		if (nk_button_label(ctx, "button")) {
			fprintf(stderr, "button pressed\n");
		}
	    }
	nk_end(ctx);
	glViewport(0, 0, app->width, app->height);
	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	nk_eglapp_render(app, NK_ANTI_ALIASING_ON,
		      MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
	eglSwapBuffers(app->eglenv->egl_display, app->eglsurface);
}
