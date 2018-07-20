#ifndef NK_EGL_BACKEND_H
#define NK_EGL_BACKEND_H

#include <EGL/egl.h>
#include <GL/gl.h>


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

/**
 * as right now we all know that
 */
struct nk_egl_backend {
	EGLDisplay egl_display;
	EGLContext egl_context;
	struct wl_display *wl_display;
	EGLConfig config;

};


#ifdef __cplusplus
}
#endif


#endif
