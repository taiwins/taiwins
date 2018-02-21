#ifndef TW_EGL_IFACE_H
#define TW_EGL_IFACE_H

#include <EGL/egl.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//maybe move this to wl_globals later
struct egl_env {
	EGLDisplay egl_display;
	EGLContext egl_context;
	struct wl_display *wl_display;
};

bool egl_env_init(struct egl_env *env, struct wl_display *disp);

void egl_env_end(struct egl_env *env);


#ifdef __cplusplus
}
#endif





#endif /* EOF */
