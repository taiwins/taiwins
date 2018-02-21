#ifndef TW_EGL_IFACE_H
#define TW_EGL_IFACE_H

#include <EGL/egl.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdbool.h>

#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif

//maybe move this to wl_globals later
struct egl_context {
	EGLDisplay egl_display;
	struct wl_display *wl_display;
};

bool egl_context_init(struct egl_context *ctxt);


#ifdef __cplusplus
}
#endif





#endif /* EOF */
