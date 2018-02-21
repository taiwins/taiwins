#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include "egl.h"

static const EGLint egl_context_attribs[] = {
	EGL_CONTEXT_MAJOR_VERSION, 3,
	EGL_CONTEXT_MINOR_VERSION, 3,
	EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
	EGL_NONE,
};

/* this is the required attributes we need to satisfy */
static const EGLint egl_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
	EGL_NONE,
};

static void
debug_egl_config_attribs(EGLDisplay dsp, EGLConfig cfg)
{
	int size;
	bool yes;
	eglGetConfigAttrib(dsp, cfg,
			   EGL_BUFFER_SIZE, &size);
	fprintf(stderr, "\tcfg %p has buffer size %d\n", cfg, size);
	yes = eglGetConfigAttrib(dsp, cfg, EGL_BIND_TO_TEXTURE_RGBA, NULL);
	fprintf(stderr, "\tcfg %p can %s bound to the rgba buffer", cfg,
		yes ? "" : "not");
}



bool
egl_env_init(struct egl_env *env, struct wl_display *d)
{
#ifndef EGL_VERSION_1_5
	fprintf(stderr, "the feature requires EGL 1.5 and it is not supported\n");
	return false;
#endif
	env->wl_display = d;
	EGLint major, minor;
	EGLint n;
	EGLConfig egl_cfg;
	EGLint *context_attribute = NULL;
	env->egl_display = eglGetDisplay((EGLNativeDisplayType)env->wl_display);
	if (env->egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "cannot create egl display\n");
	} else {
		fprintf(stderr, "egl display created\n");
	}
	if (eglInitialize(env->egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "there is a problem initialize the egl\n");
		return false;
	}
	eglGetConfigs(env->egl_display, NULL, 0, &n);
	fprintf(stderr, "egl has %d configures\n", n);

	if (!eglChooseConfig(env->egl_display, egl_config_attribs, &egl_cfg, 1, &n)) {
		fprintf(stderr, "couldn't choose opengl configure\n");
		return false;
	}
	eglBindAPI(EGL_OPENGL_API);
	env->egl_context = eglCreateContext(env->egl_display, egl_cfg, EGL_NO_CONTEXT, egl_context_attribs);
	if (env->egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "no egl context created\n");
		return false;
	}
	return true;
}


void
egl_env_end(struct egl_env *env)
{
	eglDestroyContext(env->egl_display, env->egl_context);
	eglTerminate(env->egl_display);
}
