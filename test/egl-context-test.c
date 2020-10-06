#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>


int main(int argc, char *argv[])
{
	//TODO; we will want to test wayland, X11, and headless, for now, lets
	//test headless
	tw_logger_use_file(stderr);

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_NONE,
	};
	struct tw_egl egl = {0};
	struct tw_egl_options opts = {
		.platform = EGL_PLATFORM_SURFACELESS_MESA,
		.native_display = EGL_DEFAULT_DISPLAY,
		.visual_id = 0,
		.context_attribs = config_attribs,
	};

	if (!tw_egl_init(&egl, &opts)) {
		exit(EXIT_FAILURE);
	}
	if (!tw_egl_check_gl_ext(&egl, "GL_OES_rgba8_rgba8"))
		tw_logl("we do not have GL_RGBA8_OES");

	tw_egl_fini(&egl);

	return 0;
}
