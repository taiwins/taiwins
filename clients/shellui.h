#ifndef SHELL_UI_H
#define SHELL_UI_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>

#include "ui.h"
//pull in the nuklear headers so we can access eglapp
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "../3rdparties/nuklear/nuklear.h"


#ifdef __cplusplus
extern "C" {
#endif


struct eglapp_icon {
	//you need to know the size of it
	cairo_surface_t *isurf;
	cairo_t *ctxt;
	struct bbox box; //the location of the icon in the panel
	void (*update_icon)(struct eglapp_icon *);
};

#ifndef NK_EGLAPP_TEXT_MAX
#define NK_EGLAPP_TEXT_MAX 256
#endif

struct eglapp {
	//we need to have an icon as well,
	struct app_surface surface;
	struct eglapp_icon icon;
	lua_State *L;

	void (*draw_widget)(struct eglapp *);
	const struct egl_env *eglenv;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;

	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	struct nk_vec2 fb_scale;
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


//the function work with
int update_icon_event(void *data);


struct egl_env;
struct eglapp;
//because you don't want to expose it, so we will have to create a lot help functions

struct eglapp *eglapp_from_icon(struct eglapp_icon *icon);
struct app_surface *appsurface_from_icon(struct eglapp_icon *icon);
struct bbox *icon_get_available_space(struct eglapp_icon *app);
struct eglapp_icon *icon_from_eglapp(struct eglapp *app);
struct app_surface *appsurface_from_app(struct eglapp *app);

//later on you will need to use these function to build apps
void eglapp_init_with_funcs(struct eglapp *app,
			    void (*update_icon)(struct eglapp_icon *),
			    void (*draw_widget)(struct eglapp *));
void eglapp_init_with_script(struct eglapp *app,
			     const char *script);

void eglapp_launch(struct eglapp *app, struct egl_env *env, struct wl_compositor *compositor);

void eglapp_destroy(struct eglapp *app);

struct eglapp *
eglapp_addtolist(struct app_surface *panel, vector_t *widgets);


//sample function
void calendar_icon(struct eglapp_icon *icon);

#ifdef __cplusplus
}
#endif


#endif
