#include <time.h>
#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
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

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "../3rdparties/nuklear/nuklear.h"

/*
 * ==============================================================
 *
 *                          EGL environment
 *
 * ===============================================================
 */

static const EGLint egl_context_attribs[] = {
	EGL_CONTEXT_MAJOR_VERSION, 3,
	EGL_CONTEXT_MINOR_VERSION, 3,
	EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
	EGL_NONE,
};

/* this is the required attributes we need to satisfy */
static const EGLint egl_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
	EGL_NONE,
};

static void
debug_egl_config_attribs(EGLDisplay dsp, EGLConfig cfg)
{
	int size;
	bool yes;
	eglGetConfigAttrib(dsp, cfg,
			   EGL_BUFFER_SIZE, &size);
	fprintf(stderr, "\tcfg %p has buffer size %d\n", cfg, size);
	yes = eglGetConfigAttrib(dsp, cfg, EGL_BIND_TO_TEXTURE_RGBA, NULL);
	fprintf(stderr, "\tcfg %p can %s bound to the rgba buffer", cfg,
		yes ? "" : "not");
}

bool
egl_env_init(struct egl_env *env, struct wl_display *d)
{
#ifndef EGL_VERSION_1_5
	fprintf(stderr, "the feature requires EGL 1.5 and it is not supported\n");
	return false;
#endif
	env->wl_display = d;
	EGLint major, minor;
	EGLint n;
	EGLConfig egl_cfg;
	EGLint *context_attribute = NULL;
	env->egl_display = eglGetDisplay((EGLNativeDisplayType)env->wl_display);
	if (env->egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "cannot create egl display\n");
	} else {
		fprintf(stderr, "egl display created\n");
	}
	if (eglInitialize(env->egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "there is a problem initialize the egl\n");
		return false;
	}
	eglGetConfigs(env->egl_display, NULL, 0, &n);
	fprintf(stderr, "egl has %d configures\n", n);

	if (!eglChooseConfig(env->egl_display, egl_config_attribs, &egl_cfg, 1, &n)) {
		fprintf(stderr, "couldn't choose opengl configure\n");
		return false;
	}
	eglBindAPI(EGL_OPENGL_API);
	env->egl_context = eglCreateContext(env->egl_display, egl_cfg, EGL_NO_CONTEXT, egl_context_attribs);
	if (env->egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "no egl context created\n");
		return false;
	}
	env->config = egl_cfg;
	//now we can try to create a program and see if I need
	return true;
}

void
egl_env_end(struct egl_env *env)
{
	eglDestroyContext(env->egl_display, env->egl_context);
	eglTerminate(env->egl_display);
}

/*
 * ===============================================================
 *
 *                 EGL application book-keeping
 *
 * ===============================================================
 */
#ifndef NK_EGLAPP_TEXT_MAX
#define NK_EGLAPP_TEXT_MAX 256
#endif

struct eglapp {
	//we need to have an icon as well,
	struct app_surface surface;
	//app specific
	struct eglapp_icon icon;
	//a temperary info mation, useless after app initialized
	struct bbox available_place;
	lua_State *L;

	void (*draw_widget)(struct eglapp *);

	struct shell_panel *panel;

	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	struct nk_vec2 fb_scale;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;
	unsigned int text[NK_EGLAPP_TEXT_MAX];
	unsigned int text_len;
	//now we can add all those fancy opengl stuff
	GLuint glprog, vs, fs;//actually, we can evider vs, fs
	GLuint vao, vbo, ebo;
	//uniforms
	GLint uniform_tex;
	GLint uniform_proj;
	GLuint font_tex;
	GLint attrib_pos;
	GLint attrib_uv;
	GLint attrib_col;

	int width, height;
	int cx, cy; //cursor location
};

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
struct bbox *
icon_get_available_space(struct eglapp_icon *icon)
{
	struct eglapp *app = container_of(icon, struct eglapp, icon);
	return &app->available_place;

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




void static
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

static void
eglapp_cursor_motion_cb(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
//	nk_input_begin(ctx);
	//now we need a key translation library...
	nk_input_motion(ctx, sx, sy);
//	nk_input_end(ctx);
}

static void
eglapp_cursor_button_cb(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
	nk_input_begin(ctx);
	nk_input_button(ctx, (btn) ? NK_BUTTON_LEFT : NK_BUTTON_RIGHT, sx, sy, true);
	nk_input_end(ctx);
}

static void
eglapp_cursor_axis_cb(struct app_surface *surf, int speed, int direction, uint32_t sx, uint32_t sy)
{
	struct eglapp *app = container_of(surf, struct eglapp, surface);
	struct nk_context *ctx = &app->ctx;
	nk_input_begin(ctx);
	nk_input_scroll(ctx, nk_vec2(speed * direction, speed *(1-direction)));
	nk_input_end(ctx);
}



void
eglapp_init_with_funcs(struct eglapp *app,
		       void (*update_icon)(struct eglapp_icon *),
		       void (*draw_widget)(struct eglapp *))
{
	int icon_width;
	struct eglapp_icon *icon = &app->icon;
	//call updating the first time, so we know how big it is
	update_icon(&app->icon);
	//update geometry info so later on we know where to put it
	icon_width = cairo_image_surface_get_width(icon->isurf);
	icon->box.x = app->available_place.w - icon_width;
	icon->box.y = 0;
	icon->box.w = icon_width;
	icon->box.h = app->panel->panelsurf.h;
	app->available_place = icon->box;
	app->width = 50;
	app->width = 50;
	app->icon.update_icon = update_icon;
	app->draw_widget = draw_widget;
	//TODO, temp code again, you need to remove this
	eglapp_update_icon(app);
	//setup callbacks
	app->surface.keycb = eglapp_key_cb;
	app->surface.pointron = eglapp_cursor_motion_cb;
	app->surface.pointrbtn = eglapp_cursor_button_cb;
	app->surface.pointraxis = eglapp_cursor_axis_cb;
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
eglapp_addtolist(struct shell_panel *panel)
{
	struct bbox box;
	vector_t *widgets = &panel->widgets;
	struct eglapp *lastapp, *newapp;

	if (!widgets->elems)
		vector_init(widgets, sizeof(struct eglapp), _free_eglapp);
	lastapp = (struct eglapp *)vector_at(widgets, widgets->len-1);
	//decide where to put the icon
	if (!lastapp) {
		box = (struct bbox) { .x=0, .y=0,
				      .w=panel->panelsurf.w,
				      .h=panel->panelsurf.h};
	} else {
		box = (struct bbox) { .x=0, .y=0,
				      .w=lastapp->icon.box.x,
				      .h=panel->panelsurf.h};
	}
	newapp = (struct eglapp *)vector_newelem(widgets);
	memset(newapp, 0, sizeof(*newapp));
	newapp->available_place = box;
	newapp->icon.box = (struct bbox){0};
	newapp->panel = panel;
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

void
eglapp_update_icon(struct eglapp *app)
{
	struct shell_panel *panel = app->panel;
	void *buffer = shm_pool_buffer_access(panel->panelsurf.wl_buffer);
	//you don't event know the size of it
	cairo_format_t format = translate_wl_shm_format(panel->format);
	int stride = cairo_format_stride_for_width(format, panel->panelsurf.w);
	cairo_surface_t *psurface = cairo_image_surface_create_for_data(
		(unsigned char *)buffer, format, panel->panelsurf.w, panel->panelsurf.h, stride);
	cairo_t *context = cairo_create(psurface);
//	cairo_move_to(context, app->app.px, app->app.py);

	cairo_set_source_surface(context, app->icon.isurf, app->icon.box.x, app->icon.box.y);
	cairo_paint(context);
	fprintf(stderr, "the app coordinate is %d %d\n", app->icon.box.x, app->icon.box.y);
	wl_surface_damage_buffer(app->panel->panelsurf.wl_surface, app->icon.box.x, app->icon.box.y,
				 app->icon.box.w, app->icon.box.h);
	wl_surface_commit(app->panel->panelsurf.wl_surface);

//	cairo_surface_write_to_png(psurface, "/tmp/debug.png");
	cairo_destroy(context);
	cairo_surface_destroy(psurface);
}

#define NK_SHADER_VERSION "#version 330 core\n"
struct egl_nk_vertex {
	float position[2];
	float uv[2];
	nk_byte col[4];
};

//okay, I can only create program after creating a window
void
eglapp_launch(struct eglapp *app, struct egl_env *env, struct wl_compositor *compositor)
{
	GLint status, loglen;

	app->surface.wl_surface = wl_compositor_create_surface(compositor);
	//I need to insert some call before creating window, otherwise, we wouldn't know where it goes, we will see
	app->eglwin = wl_egl_window_create(app->surface.wl_surface, 100, 100);
	app->eglsurface = eglCreateWindowSurface(env->egl_display, env->config, (EGLNativeWindowType)app->eglwin, NULL);
	if (eglMakeCurrent(env->egl_display, app->eglsurface, app->eglsurface, env->egl_context)) {
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
//	fprintf(stderr, "the gl program with id %u %u %u\n", app->glprog, app->vs, app->fs);
	assert(glGetError() == GL_NO_ERROR);
//	fprintf(stderr, "the error number %d\n", );
	//compile shader
	glShaderSource(app->vs, 1, &vertex_shader, 0);
	glShaderSource(app->fs, 1, &fragment_shader, 0);
	glCompileShader(app->vs);
	glCompileShader(app->fs);
	glGetShaderiv(app->vs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(app->vs, GL_INFO_LOG_LENGTH, &loglen);
	/* if (status != GL_TRUE) { /\*  *\/ */
	/*	char err_msg[loglen]; */
	/*	glGetShaderInfoLog(app->vs, loglen, NULL, err_msg); */
	/*	fprintf(stderr, "vertex shader compile fails: %s\n", err_msg); */
	/* } */
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
}


NK_INTERN void
nk_egl_upload_atlas(struct eglapp *app, const void *image, int width, int height)
{
	glGenTextures(1, &app->font_tex);
	glBindTexture(GL_TEXTURE_2D, app->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height,
		     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
}


NK_API void
nk_egl_render(struct eglapp *app, enum nk_anti_aliasing AA, int max_vertex_buffer,
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
nk_egl_new_frame(struct eglapp *app)
{
	struct nk_context *ctx = &app->ctx;
	struct wl_egl_window *win = app->eglwin;

	nk_input_begin(ctx);
	for (int i = 0; i < app->text_len; i++)
		nk_input_unicode(ctx, app->text[i]);
	nk_input_end(ctx);
	//okay, now we can draw it
}





///////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Lua C callbacks////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

//lua uses this to render svg to the surface, so lua can use it
static int
egl_load_svg(lua_State *L)
{
	struct eglapp **ptr, *app;
	const char *string;
	//lua call this with (eglapp, function)
	int nargs = lua_gettop(L);
	ptr = lua_touserdata(L, -2);
	string = lua_tostring(L, -1);

	app = *ptr;
	RsvgHandle *handle = rsvg_handle_new_from_file(string, NULL);
	rsvg_handle_render_cairo(handle, app->icon.ctxt);
	rsvg_handle_close(handle, NULL);

	return 0;
	//then afterwords, we should have panel to use it.
}

//it can be a callback

//this function is used for lua code to actively update the icon
static int
lua_eglapp_update_icon(lua_State *L)
{
	struct eglapp **ptr, *app;
//	const char *string;
	ptr = lua_touserdata(L, -1);
	app = *ptr;
	eglapp_update_icon(app);
}

//create an example application, calendar
