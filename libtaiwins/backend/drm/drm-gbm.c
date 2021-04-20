/*
 * drm-gbm.c - taiwins server drm-gbm functions
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

#include <assert.h>
#include <string.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-util.h>
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/drm_formats.h>

#include "internal.h"

const struct tw_drm_gpu_impl tw_gpu_gbm_impl;

static inline struct gbm_device *
tw_drm_get_gbm_device(struct tw_drm_gpu *gpu)
{
	assert(gpu->impl == &tw_gpu_gbm_impl);
	return (struct gbm_device *)(void *)gpu->device;
}

static inline struct gbm_surface *
tw_drm_output_get_gbm_surface(struct tw_drm_display *output)
{
	assert(output->gpu->impl == &tw_gpu_gbm_impl);
	return (struct gbm_surface *)(void *)output->handle;
}

static inline bool
tw_drm_output_invalid_active_state(struct tw_drm_display *output)
{
	return (!output->crtc || !output->primary_plane);
}

static inline struct gbm_bo *
tw_drm_fb_get_gbm_bo(struct tw_drm_fb *fb)
{
	return (struct gbm_bo *)(void *)fb->handle;
}

static inline struct gbm_surface *
tw_drm_create_gbm_surface(struct gbm_device *dev, uint32_t w, uint32_t h,
                          uint32_t format, int n_mods, uint64_t *modifiers,
                          uint32_t flags)
{
	struct gbm_surface *surf =
		gbm_surface_create_with_modifiers(dev, w, h, format,
		                                  modifiers, n_mods);
	if (!surf)
		surf = gbm_surface_create(dev, w, h, format, flags);

	return surf;
}

static void
free_fb(struct gbm_bo *bo, void *data)
{
	uint32_t id = (uintptr_t)data;

	if (id) {
		struct gbm_device *gbm = gbm_bo_get_device(bo);
		drmModeRmFB(gbm_device_get_fd(gbm), id);
	}
}

static uint32_t
tw_drm_gbm_get_fb(struct gbm_bo *bo)
{
	uint32_t fbid = (uintptr_t)gbm_bo_get_user_data(bo);

	struct gbm_device *gbm = gbm_bo_get_device(bo);
	int fd = gbm_device_get_fd(gbm);
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);

	uint32_t handles[4] = {0}, strides[4] = {0}, offsets[4] = {0};
	uint64_t modifiers[4] = {0};

	if (fbid)
		return fbid;

	for (int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
		modifiers[i]= gbm_bo_get_modifier(bo);
	}
	if (gbm_bo_get_modifier(bo) != DRM_FORMAT_MOD_INVALID) {
		if (drmModeAddFB2WithModifiers(fd, width, height, format,
		                               handles, strides, offsets,
		                               modifiers, &fbid,
		                               DRM_MODE_FB_MODIFIERS))
			tw_logl_level(TW_LOG_WARN, "Failing add framebuffer");
	} else {
		if (drmModeAddFB2(fd, width, height, format, handles, strides,
		                  offsets, &fbid, 0)) {
			tw_logl_level(TW_LOG_ERRO, "Failing add framebuffer");
		}
	}
	gbm_bo_set_user_data(bo, (void *)(uintptr_t)fbid, free_fb);
	return fbid;
}

static inline void
tw_drm_gbm_write_fb(struct tw_drm_fb *fb, struct gbm_bo *bo)
{
	fb->fb = tw_drm_gbm_get_fb(bo);
	fb->w = gbm_bo_get_width(bo);
	fb->h = gbm_bo_get_height(bo);
	fb->handle = (uintptr_t)(void *)bo;
	fb->locked = true;
}

static const struct tw_drm_format *
tw_drm_gbm_pick_format(struct tw_drm_plane *plane, uint32_t *visual)
{
	const struct tw_drm_format *format = tw_drm_format_find(&plane->formats,
	                                                        *visual);
	if (!format) {
		//strip alpha format
		*visual = (*visual == DRM_FORMAT_ARGB8888) ?
			DRM_FORMAT_XRGB8888 : DRM_FORMAT_INVALID;
		format = tw_drm_format_find(&plane->formats, *visual);
		if (!format) {
			tw_logl_level(TW_LOG_ERRO, "no drm format available for"
			              "gbm_surface");
			return NULL;
		}
	}
	return format;
}

/******************************************************************************
 * tw_gpu_gbm_impl
 *****************************************************************************/

static bool
handle_render_pending(struct tw_drm_display *output,
                      struct tw_kms_state *pending)
{
	struct gbm_surface *gbm_surface =
		tw_drm_output_get_gbm_surface(output);
	struct gbm_bo *next_bo = NULL;

	if (tw_drm_output_invalid_active_state(output))
		return false;

	next_bo = gbm_surface_lock_front_buffer(gbm_surface);
	if (!next_bo) {
		tw_log_level(TW_LOG_ERRO, "Failed to lock front buffer");
		return false;
	}
	tw_drm_gbm_write_fb(&pending->fb, next_bo);
	return true;
}

static void
handle_release_gbm_bo(struct tw_drm_display *output, struct tw_drm_fb *fb)
{
	struct gbm_surface *surf = tw_drm_output_get_gbm_surface(output);
	struct gbm_bo *bo = tw_drm_fb_get_gbm_bo(fb);
	if (bo && surf) {
		gbm_surface_release_buffer(surf, bo);
		fb->locked = false;
	}
}

static void
handle_end_gbm_display(struct tw_drm_display *output)
{
	struct gbm_surface *surface = tw_drm_output_get_gbm_surface(output);

	if (surface != NULL) {
		tw_render_presentable_fini(&output->output.surface,
		                           output->drm->base.ctx);
		gbm_surface_destroy(surface);
		output->handle = (uintptr_t)NULL;
	}
}

/*
 * creating a buffer for for the output. We will allocate a buffer as same size
 * as the selected mode. In general, we allocate this buffer for a given plane.
 */
static bool
handle_allocate_display_gbm_surface(struct tw_drm_display *output,
                                    drmModeModeInfo *mode)
{
	struct gbm_surface *surface;
	struct tw_drm_gpu *gpu = output->gpu;
	struct gbm_device *gbm = tw_drm_get_gbm_device(gpu);
	unsigned w = mode->hdisplay;
	unsigned h = mode->vdisplay;
	//TODO: find a suitable format instead of hardcoding
	uint32_t visual_id = DRM_FORMAT_ARGB8888;
	uint32_t scanout_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
	struct tw_drm_plane *plane = output->primary_plane;
	const struct tw_drm_format *format =
		tw_drm_gbm_pick_format(plane, &visual_id);
	const struct tw_drm_modifier *mods =
		tw_drm_modifiers_get(&plane->formats, format);

	//ensure we have at least one mods
	uint64_t modifiers[format->len+1];
	for (int i = 0; i < format->len; i++)
		modifiers[i] = mods[i].modifier;

	handle_end_gbm_display(output);
	surface = tw_drm_create_gbm_surface(gbm, w, h, visual_id,
	                                    format->len, modifiers,
	                                    scanout_flags);
	tw_render_presentable_init_window(&output->output.surface,
	                                  output->drm->base.ctx,
	                                  surface, format->fmt);
	output->handle = (uintptr_t)(void *)surface;
	return true;
}

static bool
handle_get_gpu_device(struct tw_drm_gpu *gpu,
                      const struct tw_login_gpu *login_gpu)
{
	gpu->device = (uintptr_t)(void *)gbm_create_device(login_gpu->fd);
	if (!gpu->device) {
		tw_logl_level(TW_LOG_ERRO, "Failed to create gbm device");
		return false;
	}
	return true;
}

static void
handle_free_gpu_device(struct tw_drm_gpu *gpu)
{
	struct gbm_device *gbm = tw_drm_get_gbm_device(gpu);
	gbm_device_destroy(gbm);
}

static const struct tw_egl_options *
handle_gen_egl_params(struct tw_drm_gpu *gpu)
{
	static struct tw_egl_options egl_opts = {
		.platform = EGL_PLATFORM_GBM_KHR,
	};
	egl_opts.native_display = tw_drm_get_gbm_device(gpu);
	return &egl_opts;
}

const struct tw_drm_gpu_impl tw_gpu_gbm_impl = {
    .type = TW_DRM_PLATFORM_GBM,
    .get_gpu_device = handle_get_gpu_device,
    .free_gpu_device = handle_free_gpu_device,
    .gen_egl_params = handle_gen_egl_params,
    .allocate_fbs = handle_allocate_display_gbm_surface,
    .acquire_fb = handle_render_pending,
    .release_fb = handle_release_gbm_bo,
    .free_fbs = handle_end_gbm_display,
};
