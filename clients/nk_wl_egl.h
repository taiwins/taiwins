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
#define NK_ZERO_COMMAND_MEMORY
#include "../3rdparties/nuklear/nuklear.h"

#ifndef NK_EGL_CMD_SIZE
#define NK_EGL_CMD_SIZE 4096
#endif


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

//okay, hide it
struct nk_egl_backend;

bool egl_env_init(struct egl_env *env, struct wl_display *disp);
void egl_env_end(struct egl_env *env);

typedef void (*nk_egl_draw_func_t)(struct nk_context *ctx, float widht, float height);

struct nk_egl_backend *nk_egl_create_backend(const struct egl_env *env, struct wl_surface *attach_to);
void nk_egl_launch(struct nk_egl_backend *bkend,
		   int width, int height, float scale,
		   nk_egl_draw_func_t draw_func,
		   char *char_buffer, size_t total);
void nk_egl_destroy_backend(struct nk_egl_backend *bkend);

char *nk_egl_access_text_buffer(const struct nk_context *ctx, size_t *size, size_t *ptr);


#ifdef __cplusplus
}
#endif




#endif
