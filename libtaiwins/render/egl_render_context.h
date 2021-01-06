/*
 * egl_render_context.h - taiwins egl render context header
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

#ifndef TW_EGL_RENDER_CONTEXT_H
#define TW_EGL_RENDER_CONTEXT_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/surface.h>
#include <taiwins/render_context_egl.h>
#include <taiwins/render_surface.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_render_context {
	struct tw_render_context base;
	struct tw_egl egl;
	struct wl_array pixel_formats;

	struct wl_listener dma_set;
	struct wl_listener compositor_set;
	struct wl_listener surface_created;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_get_texture2d_oes;
		PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
		PFNGLDEBUGMESSAGECONTROLKHRPROC glDebugMessageControlKHR;
		PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
		PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
	} funcs;
};

bool
tw_egl_render_context_import_buffer(struct tw_event_buffer_uploading *event,
                                    void *callback);

void
tw_gles_debug_push(struct tw_egl_render_context *ctx, const char *func);

void
tw_gles_debug_pop(struct tw_egl_render_context *ctx);

#define TW_GLES_DEBUG_PUSH(ctx) tw_gles_debug_push(ctx, __func__)
#define TW_GLES_DEBUG_POP(ctx) tw_gles_debug_pop(ctx)

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
