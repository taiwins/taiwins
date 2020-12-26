/*
 * drm_stream.c - taiwins server eglstream implementation
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

#include <stdint.h>
#include <taiwins/objects/egl.h>
#include "drm_fourcc.h"
#include "internal.h"
#include "taiwins/render_context.h"
#include "taiwins/render_output.h"

#ifndef EGL_PLATFORM_DEVICE_EXT
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#endif

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

// Those functions has to go to egl context
struct tw_egl_stream {
	EGLOutputLayerEXT plane;
	EGLStreamKHR stream;
};

bool
tw_egl_render_init_stream_surface(struct tw_render_presentable *surf,
                                  struct tw_render_context *base,
                                  uint32_t plane_id, uint32_t crtc_id,
                                  uint32_t width, uint32_t height);
bool
tw_egl_stream_flip(struct tw_render_presentable *surf,
                   struct tw_render_context *base, void *data);

static void
handle_end_stream_display(struct tw_drm_display *output)
{
	//tw_render_output_fini is called elsewhere, I could probably destroy
	//the dumbfb here
}

static bool
handle_start_stream_display(struct tw_drm_display *output)
{
	//This is probably not correct, I remember main plane requires dumb fb
	struct tw_drm_plane *plane = output->primary_plane;
	struct tw_render_context *ctx = output->drm->base.ctx;
	struct tw_drm_crtc *crtc = output->crtc;
	unsigned w = output->status.mode.hdisplay;
	unsigned h = output->status.mode.vdisplay;

	if (!crtc || !plane || !ctx)
		return false;

	return tw_egl_render_init_stream_surface(&output->output.surface, ctx,
	                                         plane->id, crtc->id, w, h);
}

static bool
handle_get_gpu_device(struct tw_drm_gpu *gpu, const struct tw_login_gpu *login)
{
	EGLDeviceEXT dev = tw_egl_device_from_path(login->path);
	if (dev == EGL_NO_DEVICE_EXT)
		return false;

	gpu->device = (uintptr_t)(void *)dev;
	gpu->visual_id = DRM_FORMAT_ARGB8888;
	return (dev != EGL_NO_DEVICE_EXT);
}

static void
handle_free_gpu_device(struct tw_drm_gpu *gpu)
{
	//there seems to be no function for freeing EGLDevice
	gpu->device = (uintptr_t)(void *)EGL_NO_DEVICE_EXT;
}

static const struct tw_egl_options *
handle_gen_egl_params(struct tw_drm_gpu *gpu)
{
	static struct tw_egl_options egl_opts = {
		.platform = EGL_PLATFORM_DEVICE_EXT,
	};
	static EGLint egl_platform_attribs[] = {
		EGL_DRM_MASTER_FD_EXT, 0,
                EGL_NONE
	};

	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	egl_platform_attribs[1] = gpu->gpu_fd;
	egl_opts.context_attribs = egl_config_attribs;
	egl_opts.platform_attribs = egl_platform_attribs;
	egl_opts.visual_id = gpu->visual_id;
	egl_opts.native_display = (void *)gpu->device;

	return &egl_opts;
}

const struct tw_drm_gpu_impl tw_gpu_stream_impl = {
    .type = TW_DRM_PLATFORM_STREAM,
    .get_gpu_device = handle_get_gpu_device,
    .free_gpu_device = handle_free_gpu_device,
    .gen_egl_params = handle_gen_egl_params,
    .start_display = handle_start_stream_display,
    .end_display = handle_end_stream_display,
};
