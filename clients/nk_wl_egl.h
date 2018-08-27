#ifndef NK_EGL_BACKEND_H
#define NK_EGL_BACKEND_H
#include <stdbool.h>
#include <stdarg.h>
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

//to output the key
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>


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

typedef void (*nk_egl_draw_func_t)(struct nk_context *ctx, float width, float height, void *data);
typedef void (*nk_egl_postcall_t)(void *userdata);

//TODO: change wl_surface to appsurface or maintain the appsurface inside nk_egl_backend
struct nk_egl_backend *nk_egl_create_backend(const struct egl_env *env, struct wl_surface *attach_to);
void nk_egl_launch(struct nk_egl_backend *bkend,
		   int width, int height, float scale,
		   nk_egl_draw_func_t draw_func, void *data);
void nk_egl_resize(struct nk_egl_backend *bkend,
		   int width, int height);
void nk_egl_destroy_backend(struct nk_egl_backend *bkend);
//tell the nk_egl_backend to run a specific task after the the rendering,
//provides also an option to clean up the state as well, it get's cleaned after evaluated.
void nk_egl_add_idle(struct nk_context *ctx,
		     void (*task)(void *user_data));


//yeah, we can do in this way, but we can also
xkb_keysym_t nk_egl_get_keyinput(struct nk_context *ctx);
//this call back writes the framebuffer to a image file.
bool nk_egl_get_btn(struct nk_context *ctx, enum nk_buttons *btn, uint32_t *sx, uint32_t *sy);


#ifdef __DEBUG
void nk_egl_capture_framebuffer(struct nk_context *ctx, const char *path);
#endif


#ifdef __cplusplus
}
#endif




#endif
