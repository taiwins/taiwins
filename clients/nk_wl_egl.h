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

#include <stdio.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
#include "egl.h"
#include "../config.h"


#ifdef __cplusplus
extern "C" {
#endif

//okay, hide it
struct app_surface;
struct nk_egl_backend;
struct taiwins_theme;

typedef void (*nk_egl_draw_func_t)(struct nk_context *ctx, float width, float height, struct app_surface *app);
typedef void (*nk_egl_postcall_t)(struct app_surface *app);

struct nk_egl_backend *nk_egl_create_backend(const struct egl_env *env);
void nk_egl_destroy_backend(struct nk_egl_backend *bkend);

void nk_egl_impl_app_surface(struct app_surface *surf, struct nk_egl_backend *bkend,
			     nk_egl_draw_func_t draw_func,
			     uint32_t w, uint32_t h, uint32_t px, uint32_t py);

void nk_egl_update(struct nk_egl_backend *bkend);

DEPRECATED(void nk_egl_launch(struct nk_egl_backend *bkend, struct app_surface *app,
			     nk_egl_draw_func_t draw_func, void *data));

DEPRECATED(void nk_egl_close(struct nk_egl_backend *bkend, struct app_surface *app_surface));


//tell the nk_egl_backend to run a specific task after the the rendering,
//provides also an option to clean up the state as well, it get's cleaned after evaluated.
void nk_egl_add_idle(struct nk_context *ctx,
		     void (*task)(void *user_data));


/* these two calls are not necessary */
xkb_keysym_t nk_egl_get_keyinput(struct nk_context *ctx);
bool nk_egl_get_btn(struct nk_context *ctx, enum nk_buttons *btn, uint32_t *sx, uint32_t *sy);


#ifdef __DEBUG
void nk_egl_capture_framebuffer(struct nk_context *ctx, const char *path);
void nk_egl_resize(struct nk_egl_backend *bkend, int32_t width, int32_t height);

void
nk_egl_debug_commands(struct nk_egl_backend *bkend);

#endif


#ifdef __cplusplus
}
#endif




#endif
