/*
 * egl_render_context.c - taiwins egl render context
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

#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pixman.h>
#include <taiwins/objects/utils.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/surface.h>

#include "egl_render_context.h"
#include "render_pipeline.h"

/******************************************************************************
 * render_context implementation
 *****************************************************************************/

static void
handle_egl_surface_destroy(struct tw_render_presentable *surf,
                           struct tw_render_context *base)
{
	struct tw_egl_render_context *ctx =
		wl_container_of(base, ctx, base);
	EGLSurface egl_surface = (EGLSurface)surf->handle;

	if (eglGetCurrentContext() == ctx->egl.context &&
	    eglGetCurrentSurface(EGL_DRAW) == egl_surface)
		tw_egl_unset_current(&ctx->egl);
	eglDestroySurface(ctx->egl.display, egl_surface);
}

/* use this if you created egl context with window surface type */
static bool
new_window_surface(struct tw_render_presentable *surf,
                   struct tw_render_context *base, void *native_surface)
{
	struct tw_egl_render_context *ctx = wl_container_of(base, ctx, base);
	EGLSurface eglsurface = EGL_NO_SURFACE;

	if (!(ctx->egl.surface_type & EGL_WINDOW_BIT))
		return false;

	eglsurface =
		ctx->egl.funcs.create_window_surface(ctx->egl.display,
		                                     ctx->egl.config,
		                                     native_surface,
		                                     NULL);
	if (eglsurface == EGL_NO_SURFACE) {
		tw_logl_level(TW_LOG_ERRO, "eglCreateWindowSurface failed");
		return false;
	}
	surf->handle = (intptr_t)eglsurface;
	surf->destroy = handle_egl_surface_destroy;

	return true;
}

/* use this if you created egl context with pbuffer window type */
static bool
new_pbuffer_surface(struct tw_render_presentable *surf,
                    struct tw_render_context *base,
                    unsigned int width, unsigned int height)
{
	struct tw_egl_render_context *ctx = wl_container_of(base, ctx, base);
	EGLSurface eglsurface = EGL_NO_SURFACE;
	const EGLint attribs[] = {
		EGL_HEIGHT, height,
		EGL_WIDTH, width,
		EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
		EGL_NONE,
	};

	if (!(ctx->egl.surface_type & EGL_PBUFFER_BIT))
		return false;

	eglsurface = eglCreatePbufferSurface(&ctx->egl.display,
	                                     &ctx->egl.config, attribs);
	if (eglsurface == EGL_NO_SURFACE) {
		tw_logl_level(TW_LOG_ERRO, "eglCreatePbufferSurface failed");
		return false;
	}
	surf->handle =  (intptr_t)eglsurface; //maybe not
	surf->destroy = handle_egl_surface_destroy;
	return true;
}

static bool
commit_egl_surface(struct tw_render_presentable *surf,
                   struct tw_render_context *base)
{
	EGLSurface surface = (EGLSurface)surf->handle;
	struct tw_egl_render_context *ctx = wl_container_of(base, ctx, base);

	eglSwapBuffers(ctx->egl.display, surface);

	return true;
}

static int
make_egl_surface_current(struct tw_render_presentable *surf,
                         struct tw_render_context *base)
{
	struct tw_egl_render_context *ctx = wl_container_of(base, ctx, base);
	return tw_egl_buffer_age(&ctx->egl, (EGLSurface)surf->handle);
}

static const struct tw_render_context_impl egl_context_impl = {
	.new_offscreen_surface = new_pbuffer_surface,
	.new_window_surface = new_window_surface,
	.commit_surface = commit_egl_surface,
	.make_current = make_egl_surface_current,
};

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_context_surface_created(struct wl_listener *listener, void *data)
{
	struct tw_surface *tw_surface = data;
	struct tw_egl_render_context *ctx =
		wl_container_of(listener, ctx, surface_created);
	struct tw_render_wl_surface *surface =
		calloc(1, sizeof(*surface));

	if (!surface) {
		wl_resource_post_no_memory(tw_surface->resource);
		return;
	}

	//I think it is better if we forward the event here
	tw_render_init_wl_surface(surface, tw_surface, &ctx->base);
	tw_surface->buffer.buffer_import.callback = ctx;
	tw_surface->buffer.buffer_import.buffer_import =
		tw_egl_render_context_import_buffer;
}

static void
notify_context_compositor_set(struct wl_listener *listener, void *data)
{
	struct tw_compositor *compositor = data;
	struct tw_egl_render_context *ctx =
		wl_container_of(listener, ctx, compositor_set);
	tw_signal_setup_listener(&compositor->surface_created,
	                         &ctx->surface_created,
	                         notify_context_surface_created);
}

static void
notify_context_dma_set(struct wl_listener *listener, void *data)
{
	struct tw_egl_render_context *ctx =
		wl_container_of(listener, ctx, dma_set);
	tw_egl_impl_linux_dmabuf(&ctx->egl, data);
}

static void
notify_context_display_destroy(struct wl_listener *listener, void *display)
{
	struct tw_egl_render_context *ctx =
		wl_container_of(listener, ctx, base.display_destroy);

	struct tw_render_pipeline *pipeline, *tmp;

	wl_signal_emit(&ctx->base.events.destroy, &ctx->base);

	tw_egl_fini(&ctx->egl);
	wl_array_release(&ctx->pixel_formats);
	wl_list_remove(&ctx->base.display_destroy.link);

	wl_list_for_each_safe(pipeline, tmp, &ctx->base.pipelines, link)
		tw_render_pipeline_destroy(pipeline);

	free(ctx);
}

/******************************************************************************
 * shm API
 *****************************************************************************/

static bool
add_wl_shm_format(struct tw_egl_render_context *ctx,
                  enum wl_shm_format format)
{
	enum wl_shm_format *f;
	wl_array_for_each(f, &ctx->pixel_formats)
		if (format == *f)
			return true;
	f = wl_array_add(&ctx->pixel_formats, sizeof(format));
	if (f) {
		*f = format;
		wl_display_add_shm_format(ctx->base.display, format);
		return true;
	}
	return false;
}

static void
init_context_formats(struct tw_egl_render_context *ctx)
{
	wl_display_init_shm(ctx->base.display);
	wl_array_init(&ctx->pixel_formats);
	add_wl_shm_format(ctx, WL_SHM_FORMAT_ABGR8888);
	add_wl_shm_format(ctx, WL_SHM_FORMAT_XBGR8888);
	add_wl_shm_format(ctx, WL_SHM_FORMAT_ARGB8888);
	add_wl_shm_format(ctx, WL_SHM_FORMAT_XRGB8888);
}

/******************************************************************************
 * gles APIs
 *****************************************************************************/

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

static bool
init_gles_externsions(struct tw_egl_render_context *ctx)
{
	if (!tw_egl_check_gl_ext(&ctx->egl, "GL_EXT_texture_format_BGRA8888")){
		tw_logl_level(TW_LOG_ERRO, "RGBA8888 is not supported.");
		return false;
	}

	if (!tw_egl_check_gl_ext(&ctx->egl, "GL_OES_EGL_image")) {
		tw_logl_level(TW_LOG_ERRO, "glEGLImageTargetTexture is not"
		              "supported");
		return false;
	}
	if (!tw_egl_check_gl_ext(&ctx->egl, "GL_OES_EGL_image_external")) {
		tw_logl_level(TW_LOG_ERRO, "external EGL Image not supported,"
		              " cannot import wl_drm/dmabuf texture");
		return false;
	} else {
		ctx->funcs.image_get_texture2d_oes =
			get_glproc("glEGLImageTargetTexture2DOES");
	}
	if (tw_egl_check_gl_ext(&ctx->egl, "GL_KHR_debug")) {
		ctx->funcs.glDebugMessageCallbackKHR =
			get_glproc("glDebugMessageCallbackKHR");
		ctx->funcs.glDebugMessageControlKHR =
			get_glproc("glDebugMessageControlKHR");
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		ctx->funcs.glDebugMessageCallbackKHR(gles2_log, NULL);

	}
	return true;
}

void
tw_gles_debug_push(struct tw_egl_render_context *ctx, const char *func)
{
	if (!ctx->funcs.glPushDebugGroupKHR)
		return;
	int len = strlen(func)+4;
	char str[len];
	snprintf(str, len, "%s: ", func);
	ctx->funcs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR,
	                               1, -1, str);
}

void
tw_gles_debug_pop(struct tw_egl_render_context *ctx)
{
	if (ctx->funcs.glPopDebugGroupKHR)
		ctx->funcs.glPopDebugGroupKHR();
}

/******************************************************************************
 * initializers
 *****************************************************************************/

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts)
{
	struct tw_egl_render_context *ctx = calloc(1, sizeof(*ctx));

	if (!ctx)
		return NULL;
	if (!tw_egl_init(&ctx->egl, opts))
		goto err_init_egl;

	if (!init_gles_externsions(ctx))
		goto err_init_egl;

	ctx->base.type = TW_RENDERER_EGL;
	ctx->base.display = display;
	ctx->base.impl = &egl_context_impl;
	wl_list_init(&ctx->base.pipelines);
	wl_signal_init(&ctx->base.events.destroy);
	wl_signal_init(&ctx->base.events.dma_set);
	wl_signal_init(&ctx->base.events.compositor_set);
	wl_signal_init(&ctx->base.events.wl_surface_dirty);
	wl_signal_init(&ctx->base.events.wl_surface_destroy);
	wl_list_init(&ctx->base.outputs);
	init_context_formats(ctx);
	tw_egl_bind_wl_display(&ctx->egl, display);

	tw_set_display_destroy_listener(display, &ctx->base.display_destroy,
	                                notify_context_display_destroy);
	tw_signal_setup_listener(&ctx->base.events.dma_set, &ctx->dma_set,
	                         notify_context_dma_set);
	tw_signal_setup_listener(&ctx->base.events.compositor_set,
	                         &ctx->compositor_set,
	                         notify_context_compositor_set);

	return &ctx->base;
err_init_egl:
	free(ctx);
	return NULL;
}
