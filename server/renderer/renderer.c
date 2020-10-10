/*
 * renderer.c - taiwins backend renderer functions
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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <wayland-util.h>

#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include "egl_shaders.h"
#include "renderer.h"

static const EGLint gles3_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
	EGL_NONE,
};

static const EGLint gles2_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE,
};

/******************************************************************************
 * interface
 *****************************************************************************/

static struct tw_renderer *
tw_renderer_from_wlr_renderer(struct wlr_renderer *wlr_renderer)
{
	struct tw_renderer *base =
		container_of(wlr_renderer, struct tw_renderer, base);
	assert(wlr_egl_is_current(base->egl));
	return base;
}

static void
base_begin(struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height)
{
	struct tw_renderer *base =
		container_of(wlr_renderer, struct tw_renderer, base);

	glViewport(0, 0, width, height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	base->viewport_w = width;
	base->viewport_h = height;
}

static void
base_end(struct wlr_renderer *wlr_renderer)
{
	tw_renderer_from_wlr_renderer(wlr_renderer);
}

static void
base_clear(struct wlr_renderer *wlr_renderer, const float color[static 4])
{
	tw_renderer_from_wlr_renderer(wlr_renderer);

	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void
base_scissor(struct wlr_renderer *wlr_renderer, struct wlr_box *box)
{
	/* this is how the damage updated is implemented */
	struct tw_renderer *base =
		tw_renderer_from_wlr_renderer(wlr_renderer);

	if (box != NULL) {
		struct wlr_box flipped;
		//first transform, then flip,
		wlr_box_transform(&flipped, box,
		                  WL_OUTPUT_TRANSFORM_FLIPPED_180,
		                  base->viewport_w, base->viewport_h);
		glEnable(GL_SCISSOR_TEST);
		glScissor(flipped.x, flipped.y, flipped.width, flipped.height);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
}

static bool
base_init_wl_display(struct wlr_renderer *wlr_renderer,
                     struct wl_display *display)
{
	struct tw_renderer *base = tw_renderer_from_wlr_renderer(wlr_renderer);

	if (base->egl->exts.bind_wayland_display_wl) {
		if (!wlr_egl_bind_display(base->egl, display)) {
			tw_logl_level(TW_LOG_ERRO, "Failed to bind wl_display");
			return false;
		}
	} else {
		tw_logl("EGL_WL_bind_wayland_display is not supported");
	}
	return true;
}

static bool
noop_render_texture_with_matrix(struct wlr_renderer *renderer,
                                struct wlr_texture *texture,
                                const struct wlr_fbox *box,
                                const float matrix[static 9], float alpha)
{
	return false;
}

static void
noop_render_quad_with_matrix(struct wlr_renderer *renderer,
                             const float color[static 4],
                             const float matrix[static 9])
{}

static void
noop_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
                                 const float color[static 4],
                                 const float matrix[static 9])
{}

static const enum wl_shm_format *
base_formats(struct wlr_renderer *wlr_renderer, size_t *len)
{
	struct tw_renderer *renderer =
		tw_renderer_from_wlr_renderer(wlr_renderer);
	*len = renderer->pixel_formats.size / sizeof(enum wl_shm_format);
	return renderer->pixel_formats.data;
}

static bool
base_format_supported(struct wlr_renderer *wlr_renderer,
                      enum wl_shm_format fmt)
{
	enum wl_shm_format *f;
	struct tw_renderer *renderer =
		tw_renderer_from_wlr_renderer(wlr_renderer);
	wl_array_for_each(f, &renderer->pixel_formats)
		if (*f == fmt)
			return true;
	return false;

}

static struct wlr_texture *
noop_texture_from_pixels(struct wlr_renderer *renderer,
                         enum wl_shm_format fmt, uint32_t stride,
                         uint32_t width, uint32_t height, const void *data)
{
	return NULL;
}

static const struct wlr_renderer_impl tw_renderer_wlr_impl = {
	.begin = base_begin,
	.end = base_end,
	.clear = base_clear,
	.scissor = base_scissor,
	.render_subtexture_with_matrix = noop_render_texture_with_matrix,
	.render_quad_with_matrix = noop_render_quad_with_matrix,
	.render_ellipse_with_matrix = noop_render_ellipse_with_matrix,
	.formats = base_formats,
	.format_supported = base_format_supported,
	.texture_from_pixels = noop_texture_from_pixels,

	.init_wl_display = base_init_wl_display,
};

/******************************************************************************
 * debug
 *****************************************************************************/

static struct gles_debug_procs {
	PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
	PFNGLDEBUGMESSAGECONTROLKHRPROC glDebugMessageControlKHR;
	PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
	PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
} s_gles_debug_procs;

static enum TW_LOG_LEVEL
gles_get_log_level(GLenum type)
{
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:
		return TW_LOG_WARN;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR:
		return TW_LOG_WARN;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:
		return TW_LOG_WARN;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:
		return TW_LOG_DBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:
		return TW_LOG_DBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:
		return TW_LOG_DBUG;
	case GL_DEBUG_TYPE_MARKER_KHR:
		return TW_LOG_DBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:
		return TW_LOG_DBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:
		return TW_LOG_DBUG;
	default:
		return TW_LOG_INFO;
	}
}

static void
gles2_log(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const GLchar *msg, const void *user)
{
	tw_logl_level(gles_get_log_level(type), "[GLES2] %s", msg);
}

void
tw_gles_debug_push(const char *func)
{
	if (!s_gles_debug_procs.glPushDebugGroupKHR)
		return;
	int len = strlen(func)+4;
	char str[len];
	snprintf(str, len, "%s: ", func);
	s_gles_debug_procs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR,
	                                       1, -1, str);
}

void
tw_gles_debug_pop(void)
{
	if (s_gles_debug_procs.glPopDebugGroupKHR)
		s_gles_debug_procs.glPopDebugGroupKHR();
}

/******************************************************************************
 * renderer
 *****************************************************************************/
static bool
add_wl_shm_format(struct tw_renderer *renderer, enum wl_shm_format format)
{
	enum wl_shm_format *f;
	wl_array_for_each(f, &renderer->pixel_formats)
		if (format == *f)
			return true;
	f = wl_array_add(&renderer->pixel_formats, sizeof(format));
	if (f) {
		*f = format;
		return true;
	}
	return false;
}

static inline bool
check_glext(const char *exts, const char *ext)
{
	//you can use strstr, or strtok_r to query the strings
	return strstr(exts, ext) != NULL;
}

static void *
get_glproc(const char *name)
{
	void *proc = (void *)eglGetProcAddress(name);
	if (!proc) {
		tw_logl_level(TW_LOG_ERRO, "eglGetProcAdress(%s) failed",
		             name);
	}
	return proc;
}

static bool
query_extensions(struct tw_renderer *renderer)
{
	const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (exts_str == NULL) {
		tw_logl_level(TW_LOG_ERRO, " failed to get gl extensions.");
		return false;
	}
	if (!check_glext(exts_str, "GL_EXT_texture_format_BGRA8888")) {
		tw_logl_level(TW_LOG_ERRO, "RGBA8888 is not supported.");
		return false;
	}
	if (!check_glext(exts_str, "GL_OES_EGL_image")) {
		tw_logl_level(TW_LOG_ERRO, "glEGLImageTargetTexture is not"
		              "supported");
		return false;
	}
	if (!check_glext(exts_str, "GL_OES_EGL_image_external")) {
		tw_logl_level(TW_LOG_ERRO, "external EGL Image not supported,"
		              " cannot import wl_drm/dmabuf texture");
		return false;
	}
	renderer->glEGLImageTargetTexture2DOES =
		get_glproc("glEGLImageTargetTexture2DOES");

	//here we are dealing with only GL extensions, egl externions such as
	//getting wl_drm dmabuff images are taking cared by wlr_egl for now.
	if (check_glext(exts_str, "GL_KHR_debug")) {
		renderer->options.enable_debug = true;
		s_gles_debug_procs.glDebugMessageCallbackKHR =
			get_glproc("glDebugMessageCallbackKHR");
		s_gles_debug_procs.glDebugMessageControlKHR =
			get_glproc("glDebugMessageControlKHR");
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		s_gles_debug_procs.glDebugMessageCallbackKHR(gles2_log, NULL);

	} else {
		renderer->options.enable_debug = false;
	}
	return true;
}

bool
tw_renderer_init(struct tw_renderer *renderer,
                 struct wlr_egl *egl, EGLenum platform,
                 void *remote_display, EGLint visual_id)
{
	bool init = false;
	if (!renderer)
		return NULL;

	init = wlr_egl_init(egl, platform, remote_display,
	                    gles3_config_attribs, visual_id);
	if (!init)
		init = wlr_egl_init(egl, platform, remote_display,
		                    gles2_config_attribs, visual_id);
	if (!init) {
		//the initialization code does plenty of work in
		//eglInitialize, eglQueryString, bind function pointers
		//I am not sure if you can call it twice though
		tw_logl("EE: wlr_egl_init failed, cannot initialize EGL");
		return false;
	}
	if (!wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL)) {
		tw_logl("EE: lwr_egl_make_current failed"
		        ", no valid EGL context");
		return false;
	}
	if (!query_extensions(renderer))
		return false;

	renderer->egl = egl;
	memcpy(&renderer->wlr_impl, &tw_renderer_wlr_impl,
	       sizeof(struct wlr_renderer_impl));

	//add basic pixel formats.
	wl_array_init(&renderer->pixel_formats);
	add_wl_shm_format(renderer, WL_SHM_FORMAT_ABGR8888);
	add_wl_shm_format(renderer, WL_SHM_FORMAT_XBGR8888);
	add_wl_shm_format(renderer, WL_SHM_FORMAT_ARGB8888);
	add_wl_shm_format(renderer, WL_SHM_FORMAT_XRGB8888);

	wl_signal_init(&renderer->events.pre_output_render);
	wl_signal_init(&renderer->events.post_ouptut_render);
	wl_signal_init(&renderer->events.pre_view_render);
	wl_signal_init(&renderer->events.post_view_render);

	wlr_renderer_init(&renderer->base, &renderer->wlr_impl);

	return true;
}

void
tw_renderer_fini(struct tw_renderer *renderer)
{
	if (renderer->options.enable_debug) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		s_gles_debug_procs.glDebugMessageCallbackKHR(NULL, NULL);
	}
	wl_array_release(&renderer->pixel_formats);
	wlr_egl_unset_current(renderer->egl);
	renderer->egl = NULL;
}
