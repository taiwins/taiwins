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

#include "egl.h"
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

#define NK_SHADER_VERSION "#version 330 core"

//eglapp->loadlua

//this is a cached
struct eglapp_icon {
	//you need to know the size of it
	cairo_surface_t *isurf;
	cairo_t *ctxt;
	const cairo_surface_t * (*get_icon_surf)(struct eglapp_icon *);
	//you need to have a triggle on linux, use the inotify apis
	//cairo loading icon from lua
	//now you need t
};

const cairo_surface_t *
icon_from_svg(struct eglapp_icon *icon, const char *file)
{
	//create
	RsvgHandle *handle = rsvg_handle_new_from_file(file, NULL);
	rsvg_handle_render_cairo(handle, icon->ctxt);
	rsvg_handle_close(handle, NULL);
	return icon->isurf;
}


//sample functions of calendar icons
static const cairo_surface_t *
calendar_icon(struct eglapp_icon *icon)
{
	static const char * daysoftheweek[] = {"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	char formatedtime[20];
	cairo_text_extents_t extent;
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);

	sprintf(formatedtime, "%s %2d:%2d",
		daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min);

	if (!icon->isurf) {
		icon->isurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
							 strlen(formatedtime) * 4, 16);
		icon->ctxt = cairo_create(icon->isurf);
	}
	//clean the source
	cairo_set_source_rgba(icon->ctxt, 1.0, 1.0f, 1.0f, 0.0f);
	cairo_paint(icon->ctxt);
	cairo_set_source_rgba(icon->ctxt, 0, 0, 0, 1.0);
	cairo_select_font_face(icon->ctxt, "sans",
			       CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(icon->ctxt, 12);
	cairo_text_extents(icon->ctxt, formatedtime, &extent);
	cairo_move_to(icon->ctxt, strlen(formatedtime) * 4 - extent.width/2 , 16 - extent.height/2);
	cairo_show_text(icon->ctxt, formatedtime);
	return icon->isurf;
}

static const cairo_surface_t *
battery_icon(struct eglapp_icon *icon)
{

}

//I am not sure to put it here, it depends where it get implemented
struct eglapp {
	//we need to have an icon as well,
	struct app_surface app;
	//app specific
	struct eglapp_icon icon;
	lua_State *L;

	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	struct nk_vec2 fb_scale;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;
	unsigned int text[256];
	//now we can add all those fancy opengl stuff
	int width, height;
	GLuint glprog, vs, fs;//actually, we can evider vs, fs
	GLuint vao, vbo, ebo;
	//uniforms
	GLint uniform_tex;
	GLint uniform_proj;
	GLuint font_tex;
	GLint attrib_pos;
	GLint attrib_uv;
	GLint attrib_col;
};



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


struct egl_nk_vertex {
	float position[2];
	float uv[2];
	nk_byte col[4];
};

//okay, I can only create program after creating a window
void
eglapp_launch(struct eglapp *app, struct egl_env *env, struct wl_compositor *compositor)
{
	GLint status;

	app->app.wl_surface = wl_compositor_create_surface(compositor);
	app->eglwin = wl_egl_window_create(app->app.wl_surface, 100, 100);
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
//	fprintf(stderr, "the gl program with id %u %u %u\n", prog, vert_shdr, frag_shdr);
	assert(glGetError() == GL_NO_ERROR);
//	fprintf(stderr, "the error number %d\n", error);
	//compile shader
	glShaderSource(app->vs, 1, &vertex_shader, 0);
	glShaderSource(app->fs, 1, &fragment_shader, 0);
	glCompileShader(app->vs);
	glCompileShader(app->fs);
	glGetShaderiv(app->vs, GL_COMPILE_STATUS, &status);
	assert(status == GL_TRUE);
	glGetShaderiv(app->fs, GL_COMPILE_STATUS, &status);
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

void
app_dispose(struct eglapp *app)
{
	glDeleteBuffers(1, &app->vbo);
	glDeleteBuffers(1, &app->ebo);
	glDeleteVertexArrays(1, &app->vao);
	glDeleteTextures(1, &app->font_tex);
	glDeleteShader(app->vs);
	glDeleteShader(app->fs);
	glDeleteProgram(app->glprog);
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

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

void egl_load_svg(lua_State *L)
{

}

bool
eglapp_loadLuamodule(struct eglapp *app, const char *script)
{
	int status;

	app->L = luaL_newstate();
	luaL_openlibs(app->L);
	status = luaL_loadfile(app->L, script);
	if (status)
		return false;
	//then register the lua functions

}
