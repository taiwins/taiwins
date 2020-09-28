/*
 * render_context_egl.c - taiwins egl render context
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

#include <stdint.h>
#include <stdlib.h>
#include <taiwins/objects/utils.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "egl.h"
#include "render_context.h"
#include "taiwins/objects/logger.h"


struct tw_egl_render_context {
	struct tw_render_context base;
	struct tw_egl egl;
	struct tw_render_context_impl impl;
};

//well you need to expose this pipeline though, otherwise how would you have
//instance work with it?
struct tw_egl_pipeline {
	struct tw_render_pipeline base;
};

static void
handle_egl_surface_destroy(struct tw_render_surface *surf,
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
new_window_surface(struct tw_render_surface *surf,
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
new_pbuffer_surface(struct tw_render_surface *surf,
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

static void
notify_context_display_destroy(struct wl_listener *listener, void *display)
{
	struct tw_egl_render_context *ctx =
		wl_container_of(listener, ctx, base.display_destroy);

	struct tw_egl_pipeline *pipeline, *tmp;

	tw_egl_fini(&ctx->egl);
	wl_list_remove(&ctx->base.display_destroy.link);

	wl_list_for_each_safe(pipeline, tmp, &ctx->base.pipelines, base.link) {
		//tw_egl_pipeline_destroy(pipeline);
	}

	free(ctx);
}

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts)
{
	struct tw_egl_render_context *ctx = calloc(1, sizeof(*ctx));

	if (!ctx)
		return NULL;
	if (!tw_egl_init(&ctx->egl, opts))
		goto err_init_egl;

	ctx->base.type = TW_RENDERER_EGL;
	ctx->base.display = display;
	ctx->base.impl = &ctx->impl;
	ctx->impl.new_window_surface = new_window_surface;
	ctx->impl.new_offscreen_surface = new_pbuffer_surface;
	wl_list_init(&ctx->base.pipelines);

	tw_set_display_destroy_listener(display, &ctx->base.display_destroy,
	                                notify_context_display_destroy);
	return &ctx->base;
err_init_egl:
	free(ctx);
	return NULL;
}

/** I think this will simply seat here even if we have a vulkan render context,
 EGL render context will be compulsory */
void
tw_render_context_destroy(struct tw_render_context *ctx)
{
	ctx->display_destroy.notify(&ctx->display_destroy, ctx->display);
}
