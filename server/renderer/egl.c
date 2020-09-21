/*
 * egl.c - taiwins backend egl renderer interface
 *
 * Copyright (c) 2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <taiwins/objects/logger.h>

#include "egl.h"

//we need EGL context here mostly just for import buffer and export buffers.

const char *
platform_to_extension(EGLenum platform)
{
	switch (platform) {
	case EGL_PLATFORM_GBM_KHR:
		return "gbm";
	case EGL_PLATFORM_WAYLAND_KHR:
		return "wayland";
	case EGL_PLATFORM_X11_KHR:
		return "x11";
	case EGL_PLATFORM_SURFACELESS_MESA:
		return "surfaceless";
	default:
		assert(0 && "bad EGL platform enum");
	}
}

static inline bool
check_egl_ext(const char *exts, const char *ext, bool required)
{
	if (strstr(exts, ext) == NULL) {
		tw_logl_level(required ? TW_LOG_ERRO : TW_LOG_WARN,
		              "EGL extension %s not found", ext);
		return false;
	}
	return true;
}

static bool
get_egl_proc(void *addr, const char *name)
{
	void *proc = (void *)eglGetProcAddress(name);
	if (!proc) {
		tw_logl_level(TW_LOG_ERRO, "function %s not found", name);
		return false;
	}
	*(void **)addr = proc;
	return true;
}

static bool
setup_egl_basic_exts(struct tw_egl *egl)
{
	const char *exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

	if (!exts_str) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query EGL extensions");
		return false;
	}
	//platform display
	if (!check_egl_ext(exts_str, "EGL_EXT_platform_base", true))
		return false;
	if (!get_egl_proc(&egl->funcs.get_platform_display,
	                  "eglGetPlatformDisplayEXT"))
		return false;
	if (!get_egl_proc(&egl->funcs.create_platform_win,
	                  "eglCreatePlatformWindowSurfaceEXT"))
		return false;

	return true;
}

static bool
setup_egl_display(struct tw_egl *egl, struct tw_egl_options *opts)
{
	EGLint major, minor;

	egl->display = egl->funcs.get_platform_display(opts->platform,
	                                               opts->native_display,
	                                               NULL);
	if (egl->display == EGL_NO_DISPLAY) {
		tw_logl_level(TW_LOG_ERRO, "Failed to create EGL display");
		return false;
	}
	egl->platform = opts->platform;

	if (!eglInitialize(egl->display, &major, &minor)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to initialize EGL");
		return false;
	}

	return true;
}

static bool
setup_egl_client_extensions(struct tw_egl *egl)
{
	const char *exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (!exts_str) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query EGL extensions");
		return false;
	}
	//create/destroy EGLImage
	if (check_egl_ext(exts_str, "EGL_KHR_image_base", false)) {
		if (!get_egl_proc(&egl->funcs.create_image,
		                  "eglCreateImageKHR"))
			return false;
		if (!get_egl_proc(&egl->funcs.destroy_image,
		                  "eglDestroyImageKHR"))
			return false;
	}
	//buffer age
	if (check_egl_ext(exts_str, "EGL_EXT_buffer_age", false))
		egl->query_buffer_age = true;
	//swap buffer with damage
	if (check_egl_ext(exts_str, "EGL_KHR_swap_buffers_with_damage",
	                  false)) {
		if (!get_egl_proc(&egl->funcs.swap_buffer_with_damage,
		                  "eglSwapBuffersWithDamageKHR"))
			return false;
	} else if (check_egl_ext(exts_str, "EGL_EXT_swap_buffers_with_damage",
	                         false)) {
		if (!get_egl_proc(&egl->funcs.swap_buffer_with_damage,
		                  "eglSwapBuffersWithDamageEXT"))
			return false;
	}
	//dmabuf import
	if (check_egl_ext(exts_str, "EGL_EXT_image_dma_buf_import", false) &&
	    check_egl_ext(exts_str, "EGL_EXT_image_dma_buf_import_modifiers",
	                  false)) {
		if (!get_egl_proc(&egl->funcs.query_dmabuf_formats,
		                  "eglQueryDmaBufFormatsEXT"))
			return false;
		if (!get_egl_proc(&egl->funcs.query_dmabuf_modifiers,
		                  "eglQueryDmaBufModifiersEXT"))
			return false;
	}
	//dmabuf export
	if (check_egl_ext(exts_str, "EGL_MESA_image_dma_buf_export", false)) {
		if (!get_egl_proc(&egl->funcs.export_dmabuf_image_query,
		                  "eglExportDMABUFImageQueryMESA"))
			return false;
		if (!get_egl_proc(&egl->funcs.export_dmabuf_image,
		                  "eglExportDMABUFImageMESA"))
			return false;
	}
	//bind wayland display
	if (check_egl_ext(exts_str, "EGL_WL_bind_wayland_display", false)) {
		if (!get_egl_proc(&egl->funcs.bind_wl_display,
			    "eglBindWaylandDisplayWL"))
			return false;
		if (!get_egl_proc(&egl->funcs.unbind_wl_display,
			    "eglUnbindWaylandDisplayWL"))
			return false;
		if (!get_egl_proc(&egl->funcs.query_wl_buffer,
		                  "eglQueryWaylandBufferWL"))
			return false;
	}

	return true;

}

static EGLConfig
choose_egl_config(EGLDisplay display, EGLConfig *configs, int count,
                  struct tw_egl_options *opts)
{
	if (!opts->visual_id)
		return configs[0];

	for (int i = 0; i < count; i++) {
		EGLint visual_id;
		if (!eglGetConfigAttrib(display, configs[i],
		                        EGL_NATIVE_VISUAL_ID, &visual_id))
			continue;
		if (opts->visual_id == visual_id)
			return configs[i];
	}
	return EGL_NO_CONFIG_KHR;
}

static bool
setup_egl_config(struct tw_egl *egl, struct tw_egl_options *opts)
{
	EGLint count = 0, matched = 0, ret = 0;

	ret = eglGetConfigs(egl->display, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		tw_logl_level(TW_LOG_ERRO, "eglGetConfigs failed");
		return false;
	}

	EGLConfig configs[count];

	if(eglChooseConfig(egl->display, opts->context_attribs, configs,
	                   count, &matched) == EGL_FALSE) {
		tw_logl_level(TW_LOG_ERRO, "eglChooseConfig failed");
		return false;
	}
	egl->config = choose_egl_config(egl->display, configs, matched, opts);
	if (egl->config == EGL_NO_CONFIG_KHR)
		return false;


	return true;
}

static bool
setup_egl_context(struct tw_egl *egl)
{
	EGLint attrs[16] = {EGL_CONTEXT_CLIENT_VERSION, 0};
	unsigned nattr = 2;

	const char *exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	bool ext_context_priority =
		check_egl_ext(exts_str, "EGL_IMG_context_priority", false);

	if (ext_context_priority) {
		attrs[nattr++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		attrs[nattr++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}
	attrs[nattr] = EGL_NONE;
	//try GLES 3 first
	attrs[1] = 3;
	egl->context = eglCreateContext(egl->display, egl->config,
	                                EGL_NO_CONTEXT, attrs);
	if (!egl->context) {
		attrs[1] = 2;
		egl->context = eglCreateContext(egl->display, egl->config,
		                                EGL_NO_CONTEXT, attrs);
		if (!egl->context) {
			tw_logl_level(TW_LOG_ERRO, "eglCreateContext failed");
			return false;
		}
	}

	if (ext_context_priority) {
		EGLint level = EGL_CONTEXT_PRIORITY_LOW_IMG;
		eglQueryContext(egl->display, egl->context,
		                EGL_CONTEXT_PRIORITY_LEVEL_IMG, &level);
		if (level != EGL_CONTEXT_PRIORITY_HIGH_IMG)
			tw_logl_level(TW_LOG_WARN, "failed to obtain the "
			              "high priority context");
	}
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE,
	                    EGL_NO_SURFACE, egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		eglDestroyContext(egl->display, egl->context);
		egl->context = EGL_NO_CONTEXT;
		return false;
	}

	return true;
}

static void
print_egl_info(struct tw_egl *egl)
{
	EGLint major, minor;
	char *ext = NULL, *exts_str = NULL;

	eglQueryContext(egl->display, egl->context, EGL_CONTEXT_MAJOR_VERSION,
	                &major);
	eglQueryContext(egl->display, egl->context, EGL_CONTEXT_MINOR_VERSION,
	                &minor);
	tw_logl("EGL: current EGL vendor: %s", eglQueryString(egl->display,
	                                                      EGL_VENDOR));
	tw_logl("EGL: current EGL version: %s", eglQueryString(egl->display,
	                                                       EGL_VERSION));
	tw_logl("EGL: using GLES %d.%d", major, minor);
	exts_str = strdup(eglQueryString(egl->display, EGL_EXTENSIONS));
	tw_logl("EGL extension:");
	for (ext = strtok(exts_str, " "); ext != NULL;
	     ext = strtok(NULL, " "))
		tw_logl("\t%s", ext);
	free(exts_str);
}

WL_EXPORT bool
tw_egl_init(struct tw_egl *egl, struct tw_egl_options *opts)
{
	if (!setup_egl_basic_exts(egl))
		return false;

	if (!setup_egl_display(egl, opts))
		return false;
	if (!setup_egl_client_extensions(egl))
		return false;
	if (!setup_egl_config(egl, opts))
		goto error;
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		goto error;
	//TODO setup dmabuf formats
	if (!setup_egl_context(egl))
		goto error;
	print_egl_info(egl);
	return true;
error:
	egl->config = EGL_NO_CONFIG_KHR;
	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	               EGL_NO_CONTEXT);
	if (egl->display)
		eglTerminate(egl->display);

	eglReleaseThread();
	return false;
}

WL_EXPORT void
tw_egl_fini(struct tw_egl *egl)
{
	if (!egl)
		return;

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	               EGL_NO_CONTEXT);
	if (egl->wl_display) {
		assert(egl->funcs.unbind_wl_display);
		egl->funcs.unbind_wl_display(egl->display, egl->wl_display);
	}
	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();
}

WL_EXPORT bool
tw_egl_make_current(struct tw_egl *egl, EGLSurface surface)
{
	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

WL_EXPORT bool
tw_egl_unset_current(struct tw_egl *egl)
{
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	                    egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

WL_EXPORT int
tw_egl_buffer_age(struct tw_egl *egl, EGLSurface surface)
{
	EGLint buffer_age;

	if (egl->query_buffer_age)
		return -1;
	if (!tw_egl_make_current(egl, surface))
		return -1;
	if (!eglQuerySurface(egl->display, surface,
	                     EGL_BUFFER_AGE_EXT, &buffer_age))
		return -1;
	return (int)buffer_age;
}

WL_EXPORT bool
tw_egl_check_egl_ext(struct tw_egl *egl, const char *ext)
{
	const char *exts_str = NULL;
	exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);

	if (exts_str)
		return check_egl_ext(exts_str, ext, false);
	return false;
}

WL_EXPORT bool
tw_egl_check_gl_ext(struct tw_egl *egl, const char *ext)
{
	const char *exts_str = NULL;
	bool ret = false;

	tw_egl_make_current(egl, EGL_NO_SURFACE);
	exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (!exts_str)
		tw_logl_level(TW_LOG_ERRO, "Failed to get GL_EXTENSION");
	else
		ret = check_egl_ext(exts_str, ext, false);

	//we would still be in the context
	tw_egl_unset_current(egl);

	return ret;
}
