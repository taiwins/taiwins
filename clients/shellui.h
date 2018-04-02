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

#include <wayland-taiwins-shell-client-protocol.h>
extern struct taiwins_shell *shelloftaiwins;


#ifdef __cplusplus
extern "C" {
#endif

struct shell_widget;


#define NK_SHADER_VERSION "#version 330 core\n"
//vao layout
struct egl_nk_vertex {
	float position[2];
	float uv[2];
	nk_byte col[4];
};


//they have a list of the widget on the panel
struct shell_panel {
	struct app_surface panelsurf;

	struct app_surface widget_surface;
	//current subclass impl is a vector
	vector_t widgets;
	struct shell_widget *active_one;
	//egl things
	const struct egl_env *eglenv;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;
	//vertex array and uniforms
	GLuint glprog, vs, fs;//actually, we can evider vs, fs
	GLuint vao, vbo, ebo;
	GLuint font_tex;
	GLint attrib_pos;
	GLint attrib_uv;
	GLint attrib_col;
	//uniforms
	GLint uniform_tex;
	GLint uniform_proj;
	//nuklear attributes
	//device attributes
	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	struct nk_vec2 fb_scale;
};

void panel_setup_widget_input(struct shell_panel *app);
void shell_panel_compile_widgets(struct shell_panel *panel);
void shell_panel_init_nklear(struct shell_panel *panel);
//panel widget apis
struct shell_widget *shell_panel_find_widget_at_xy(struct shell_panel *p, int xy, int y);
int shell_panel_n_widgets(struct shell_panel *p);
int shell_panel_paint_widget(struct shell_panel *panel, struct shell_widget *widget);
struct shell_widget *shell_panel_add_widget(struct shell_panel *surf);
void shell_panel_destroy_widgets(struct shell_panel *panel);


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

struct shell_widget {
	struct shell_panel *panel;
	struct eglapp_icon icon;
	bool ready_to_launch;
	//for native app, the function is defined as following.
	void (*draw_widget)(struct shell_widget *);
	//or, we will need a lua function to do it.
	lua_State *L;

	unsigned int text[NK_EGLAPP_TEXT_MAX];
	unsigned int text_len;
	uint32_t width, height;

};
//this will call the lua state of the widget
void draw_lua_widget(struct shell_widget *widget);


//later on you will need to use these function to build apps
void shell_widget_init_with_funcs(struct shell_widget *app,
			    void (*update_icon)(struct eglapp_icon *),
			    void (*draw_widget)(struct shell_widget *));
void shell_widget_init_with_script(struct shell_widget *app,
			     const char *script);

void shell_widget_launch(struct shell_widget *app);

void shell_widget_destroy(struct shell_widget *app);

/*
struct eglapp {

	//we need to have an icon as well,
	struct app_surface surface;
	struct eglapp_icon icon;
	lua_State *L;

	void (*draw_widget)(struct eglapp *);
	const struct egl_env *eglenv;
	struct wl_egl_window *eglwin;
	EGLSurface eglsurface;

	//device attributes
	struct nk_buffer cmds;
	struct nk_draw_null_texture null;
	//now we can add all those fancy opengl stuff
	GLuint glprog, vs, fs;//actually, we can evider vs, fs
	GLuint vao, vbo, ebo;
	GLuint font_tex;
	GLint attrib_pos;
	GLint attrib_uv;
	Glint attrib_col;
	//uniforms
	GLint uniform_tex;
	GLint uniform_proj;

	//nuklear attributes
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	struct nk_vec2 fb_scale;
	unsigned int text[NK_EGLAPP_TEXT_MAX];
	unsigned int text_len;

	//appsurface has its own width and height, I shouldn't need this
	int width, height;
	int cx, cy; //cursor location
};
*/

//the function that we used for events. We should have it later
int update_icon_event(void *data);

//the one sample
void calendar_icon(struct eglapp_icon *icon);
/*
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

*/
#ifdef __cplusplus
}
#endif


#endif
