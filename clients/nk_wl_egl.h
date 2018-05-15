#ifndef NK_EGL_BACKEND_H
#define NK_EGL_BACKEND_H

//pull in the nuklear headers so we can access eglapp
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "../3rdparties/nuklear/nuklear.h"

#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>


#ifdef __cplusplus
extern "C" {
#endif

//maybe move this to wl_globals later
struct egl_env {
	EGLDisplay egl_display;
	EGLContext egl_context;
	struct wl_display *wl_display;
	EGLConfig config;
};

bool egl_env_init(struct egl_env *env, struct wl_display *disp);
void egl_env_end(struct egl_env *env);

//vao layout
struct nk_egl_vertex {
	float position[2];
	float uv[2];
	float col[4];
};


struct nk_egl_backend {
	bool compiled;
	struct egl_env *env;
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
	//window params
	size_t width, height;
	struct nk_vec2 fb_scale;
};


void nk_egl_init_backend(struct nk_egl_backend *bkend, const struct egl_env *env, struct wl_surface *attach_to);
void nk_egl_end_backend(struct nk_egl_backend *bkend);
void nk_egl_launch(struct nk_egl_backend *bkend, int width, int height, struct nk_vec2 scale);

// struct nk_draw_vertex_layout_element nk_vertex_layout[4];

#ifdef __cplusplus
}
#endif




#endif
