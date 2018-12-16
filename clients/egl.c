#include <time.h>
#include <assert.h>
#include <string.h>

#ifdef _WITH_NVIDIA
#include <eglexternalplatform.h>
#endif

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>


#include <GL/gl.h>
#include <GL/glext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include <cairo/cairo.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "client.h"
#include "egl.h"


/*
 * ==============================================================
 *
 *                          EGL environment
 *
 * ===============================================================
 */

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


#ifdef _WITH_NVIDIA
//we need this entry to load the platform library
extern EGLBoolean loadEGLExternalPlatform(int major, int minor,
					  const EGLExtDriver *driver,
					  EGLExtPlatform *platform);
#endif

bool
egl_env_init(struct egl_env *env, const struct wl_display *d)
{
#ifndef EGL_VERSION_1_5
	fprintf(stderr, "the feature requires EGL 1.5 and it is not supported\n");
	return false;
#endif
	env->wl_display = d;
	EGLint major, minor;
	EGLint n;
	EGLConfig egl_cfg;

	env->egl_display = eglGetDisplay((EGLNativeDisplayType)env->wl_display);
	assert(env->egl_display);
	assert(eglInitialize(env->egl_display, &major, &minor) == EGL_TRUE);

	const char *egl_extensions = eglQueryString(env->egl_display, EGL_EXTENSIONS);
	const char *egl_vendor = eglQueryString(env->egl_display, EGL_VENDOR);
	fprintf(stderr, "egl vendor using: %s\n", egl_vendor);
	fprintf(stderr, "egl_extensions: %s\n", egl_extensions);
	eglGetConfigs(env->egl_display, NULL, 0, &n);
	fprintf(stderr, "egl has %d configures\n", n);
	assert(EGL_TRUE == eglChooseConfig(env->egl_display, egl_config_attribs, &egl_cfg, 1, &n));
	debug_egl_config_attribs(env->egl_display, egl_cfg);
	eglBindAPI(EGL_OPENGL_API);
	env->egl_context = eglCreateContext(env->egl_display,
					    egl_cfg,
					    EGL_NO_CONTEXT,
					    egl_context_attribs);
	assert(env->egl_context != EGL_NO_CONTEXT);
	env->config = egl_cfg;
	//now we can try to create a program and see if I need
	return true;
}

bool
egl_env_init_shared(struct egl_env *this, const struct egl_env *another)
{
	this->wl_display = another->wl_display;
	this->egl_display = another->egl_display;
	this->config = another->config;
	this->egl_context = eglCreateContext(this->egl_display,
					     this->config,
					     (EGLContext)another->egl_context,
					     egl_context_attribs);
	assert(this->egl_context != EGL_NO_CONTEXT);
	return true;
}


void
egl_env_end(struct egl_env *env)
{
	eglDestroyContext(env->egl_display, env->egl_context);
	eglTerminate(env->egl_display);
	eglReleaseThread();
}


void *
egl_get_egl_proc_address(const char *address)
{
	const char *extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (extensions &&
	    (strstr(extensions, "EGL_EXT_platform_wayland") ||
	     strstr(extensions, "EGL_KHR_platform_wayland"))) {
		return (void *)eglGetProcAddress(address);
	}
	return NULL;
}


EGLSurface
egl_create_platform_surface(EGLDisplay dpy, EGLConfig config,
			    void *native_window,
			    const EGLint *attrib_list)
{
	static PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC
		create_platform_window = NULL;

	if (!create_platform_window) {
		create_platform_window = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)
	    egl_get_egl_proc_address(
		"eglCreatePlatformWindowSurfaceEXT");
	}

	if (create_platform_window)
		return create_platform_window(dpy, config,
					      native_window,
					      attrib_list);

	return eglCreateWindowSurface(dpy, config,
				      (EGLNativeWindowType) native_window,
				      attrib_list);
}
