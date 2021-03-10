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
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/drm_formats.h>
#include <taiwins/render_context.h>

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
	return (output->status.active &&
	        (!output->crtc || !output->primary_plane));
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

/* static struct gbm_bo * */
/* tw_drm_gbm_render_pending(struct tw_drm_display *output) */
/* { */
/*	struct tw_drm_plane *main_plane = output->primary_plane; */
/*	struct gbm_surface *gbm_surface = */
/*		tw_drm_output_get_gbm_surface(output); */
/*	struct gbm_bo *next_bo = NULL; */

/*	next_bo = gbm_surface_lock_front_buffer(gbm_surface); */
/*	if (!next_bo) { */
/*		//TODO: We encounter on manual pageflip, if we failed to lock */
/*		//a buffer, we should use the current buffer and use the one in */
/*		//front. */
/*		tw_log_level(TW_LOG_ERRO, "Failed to lock the " */
/*		             "front buffer"); */
/*		return NULL; */
/*	} */
/*	tw_drm_gbm_write_fb(&main_plane->pending, next_bo, */
/*	                    tw_drm_gbm_get_fb(next_bo)); */
/*	return next_bo; */
/* } */

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

/******************************************************************************
 * GBM atomic page flip functions
 *****************************************************************************/

/**
 * a pageflip event has occurred, we are now free to draw the next
 * frame. Generally new_frame_event should not being called here, we should
 * rather set the render_output state to be presented.
 *
 * gbm surface has two BOs, at and the BufferSwap, we lock one BO to the
 * committed fb, upon receiving this page flipping event, it means that the
 * committed bo is now used and pending bo is freeing again for us to use.
 */
static void
atomic_pageflip(struct tw_drm_display *output, uint32_t flags)
{
	bool pass = true;
	unsigned w = 0, h = 0;
	int fd = output->gpu->gpu_fd;
	uint32_t mode_id = 0;
	int crtc_id = output->status.crtc_id;

	drmModeAtomicReq *req = NULL;
	struct tw_drm_plane *main_plane = output->primary_plane;
	struct gbm_bo *next_bo = NULL;

	if (output->status.pending & TW_DRM_PENDING_MODE)
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	else
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	assert(!tw_drm_output_invalid_active_state(output));

        if (!(req = drmModeAtomicAlloc()))
		return;

	if (output->status.active) {
		next_bo = tw_drm_gbm_render_pending(output);
		if (next_bo) {
			w = gbm_bo_get_width(next_bo);
			h = gbm_bo_get_height(next_bo);
		}
	}
	//what about other planes?
	//write the properties
	pass = tw_kms_atomic_set_plane_props(req, pass, main_plane, crtc_id,
	                                     0, 0, w, h);
	pass = tw_kms_atomic_set_connector_props(req, pass, output);
	pass = tw_kms_atomic_set_crtc_props(req, pass, output, &mode_id);

	if (pass)
		drmModeAtomicCommit(fd, req, flags, output);

	if (mode_id != 0)
		drmModeDestroyPropertyBlob(fd, mode_id);
	output->status.pending = 0;
	drmModeAtomicFree(req);
}

/******************************************************************************
 * legacy page flip handler
 *****************************************************************************/

static void
legacy_pageflip(struct tw_drm_display *output, uint32_t flags)
{
	uint32_t fb = 0;
	int fd = output->gpu->gpu_fd;
	struct gbm_bo *next_bo = NULL;
	uint32_t crtc_id = output->status.crtc_id;
	const char *name = output->output.device.name;

	assert(!tw_drm_output_invalid_active_state(output));

	if (output->status.active) {
		next_bo = tw_drm_gbm_render_pending(output);
		fb = tw_drm_gbm_get_fb(next_bo);
	}
	if (output->status.pending & TW_DRM_PENDING_MODE) {
		uint32_t on = output->status.active ?
			DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;
		uint32_t conn_id = output->conn_id;

		if (drmModeConnectorSetProperty(fd, output->conn_id,
		                                output->props.dpms, on) != 0) {
			tw_logl_level(TW_LOG_ERRO, "Failed to set %s DPMS "
			              "property", name);
			return;
		}
		if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn_id, 1,
		                   &output->status.mode)) {
			tw_logl_level(TW_LOG_ERRO, "Failed to set %s CRTC",
			              name);
			return;
		}
	}
	//TODO NO support for gamma and VRR yet.
	//TODO NO support for cursor plane yet.
	drmModeSetCursor(fd, crtc_id, 0, 0, 0);
	if (flags & DRM_MODE_PAGE_FLIP_EVENT) {
		if (drmModePageFlip(fd, crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT,
		                    output)) {
			tw_logl_level(TW_LOG_ERRO, "Failed to pageflip on %s",
			              name);
			return;
		}
	}

	output->status.pending = 0;

}

/******************************************************************************
 * tw_gpu_gbm_impl
 *****************************************************************************/

static void
handle_release_gbm_bo(struct tw_drm_display *output, struct tw_kms_state *state)
{
	struct gbm_surface *surf = tw_drm_output_get_gbm_surface(output);
	struct gbm_bo *bo = tw_drm_fb_get_gbm_bo(&state->fb);
	if (bo && surf) {
		gbm_surface_release_buffer(surf, bo);
		state->fb.locked = false;
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
handle_allocate_display_gbm_surface(struct tw_drm_display *output)
{
	struct gbm_surface *surface;
	struct tw_drm_gpu *gpu = output->gpu;
	struct gbm_device *gbm = tw_drm_get_gbm_device(gpu);
	unsigned w = output->status.mode.hdisplay;
	unsigned h = output->status.mode.vdisplay;
	uint32_t scanout_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
	struct tw_drm_plane *plane = output->primary_plane;
	const struct tw_drm_format *format =
		tw_drm_format_find(&plane->formats, gpu->visual_id);
	const struct tw_drm_modifier *mods =
		tw_drm_modifiers_get(&plane->formats, format);

	//ensure we have at least one mods
	uint64_t modifiers[format->len+1];
	for (int i = 0; i < format->len; i++)
		modifiers[i] = mods[i].modifier;

	handle_end_gbm_display(output);
	surface = tw_drm_create_gbm_surface(gbm, w, h, gpu->visual_id,
	                                     format->len, modifiers,
	                                     scanout_flags);
	tw_render_presentable_init_window(&output->output.surface,
	                                  output->drm->base.ctx,
	                                  surface);
	output->handle = (uintptr_t)(void *)surface;
	return true;
}

static void
handle_pageflip_gbm_display(struct tw_drm_display *output, uint32_t flags)
{
	struct tw_drm_gpu *gpu = output->gpu;
	if (gpu->feats & TW_DRM_CAP_ATOMIC)
		atomic_pageflip(output, flags);
	else
		legacy_pageflip(output, flags);
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
	gpu->visual_id = DRM_FORMAT_ARGB8888;
	return true;
}

static void
handle_free_gpu_device(struct tw_drm_gpu *gpu)
{
	struct gbm_device *gbm = tw_drm_get_gbm_device(gpu);
	gbm_device_destroy(gbm);
	gpu->visual_id = DRM_FORMAT_INVALID;
}

static const struct tw_egl_options *
handle_gen_egl_params(struct tw_drm_gpu *gpu)
{
	static struct tw_egl_options egl_opts = {
		.platform = EGL_PLATFORM_GBM_KHR,
	};

	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	egl_opts.context_attribs = egl_config_attribs;
	egl_opts.visual_id = gpu->visual_id;
	egl_opts.native_display = tw_drm_get_gbm_device(gpu);
	return &egl_opts;
}

const struct tw_drm_gpu_impl tw_gpu_gbm_impl = {
    .type = TW_DRM_PLATFORM_GBM,
    .get_gpu_device = handle_get_gpu_device,
    .free_gpu_device = handle_free_gpu_device,
    .gen_egl_params = handle_gen_egl_params,
    .allocate_fb = handle_allocate_display_gbm_surface,
    .end_display = handle_end_gbm_display,
    .render_pending = handle_render_pending,
    .page_flip = handle_pageflip_gbm_display,
    .vsynced = handle_release_gbm_bo,
};
