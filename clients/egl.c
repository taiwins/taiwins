#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include "egl.h"

bool
egl_context_init(struct egl_context *ctxt)
{
#ifndef EGL_VERSION_1_5
	fprintf(stderr, "the feature requires EGL 1.5 and it is not supported\n");
	return false;
#endif

	EGLint major, minor;
	EGLint n;
	EGLConfig egl_cfg;
	EGLint *context_attribute = NULL;
	const EGLint configure[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};
	const EGLint context_configure[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE,
	};
	ctxt->egl_display = eglGetDisplay((EGLNativeDisplayType)ctxt->wl_display);
	if (ctxt->egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "cannot create egl display\n");
	} else {
		fprintf(stderr, "egl display created\n");
	}
	if (eglInitialize(ctxt->egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "there is a problem initialize the egl\n");
		return false;
	}

	if (!eglChooseConfig(ctxt->egl_display, configure, &egl_cfg, 1, &n)) {
		fprintf(stderr, "couldn't choose opengl configure\n");
		return false;
	}
	return true;
}
