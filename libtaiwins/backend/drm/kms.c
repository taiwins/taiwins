/*
 * kms.c - taiwins server drm KMS functions
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

#include <gbm.h>
#include <assert.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>

#include "internal.h"

static inline void
plane_fb_init(struct tw_drm_fb *fb)
{
	fb->fb = 0;
	fb->handle = 0;
	fb->type = TW_DRM_FB_SURFACE;
}

static inline void
atomic_commit_prop_blob(int drm_fd, uint32_t *dst, uint32_t src)
{
	if (*dst == src)
		return;
	if (*dst != 0)
		drmModeDestroyPropertyBlob(drm_fd, *dst);
	*dst = src;
}

static inline void
atomic_add(drmModeAtomicReq *req, bool *pass, uint32_t id, uint32_t prop,
           uint64_t val)
{
	if (pass) {
		int ret = drmModeAtomicAddProperty(req, id, prop, val);
		*pass = *pass && (ret >= 0);
	}
}

static inline void
atomic_plane_disable(drmModeAtomicReq *req, bool *pass,
                     const struct tw_drm_plane_props *props)
{
	if (props) {
		atomic_add(req, pass, props->id, props->crtc_id, 0);
		atomic_add(req, pass, props->id, props->fb_id, 0);
	}
}

static bool
tw_kms_atomic_set_plane_fb(drmModeAtomicReq *req, bool pass,
                           struct tw_kms_state *state)
{
	const struct tw_drm_plane_props *prop = state->props_main_plane;

	if (!state->crtc.active) {
		atomic_plane_disable(req, &pass, prop);
	} else if (!prop || !state->props_crtc) {
		return false;
	} else {
		uint32_t id = prop->id;
		struct tw_drm_fb *fb = &state->fb;

		atomic_add(req, &pass, id, prop->src_x, 0);
		atomic_add(req, &pass, id, prop->src_y, 0);
		atomic_add(req, &pass, id, prop->src_w, (uint64_t)fb->w << 16);
		atomic_add(req, &pass, id, prop->src_h, (uint64_t)fb->h << 16);
		atomic_add(req, &pass, id, prop->crtc_x, fb->x);
		atomic_add(req, &pass, id, prop->crtc_y, fb->y);
		atomic_add(req, &pass, id, prop->crtc_w, fb->w);
		atomic_add(req, &pass, id, prop->crtc_h, fb->h);
		atomic_add(req, &pass, id, prop->crtc_id, state->crtc_id);
		atomic_add(req, &pass, id, prop->fb_id, fb->fb);
	}
	return pass;
}

static bool
tw_kms_atomic_set_connector_crtc(drmModeAtomicReq *req, bool pass,
                                 struct tw_kms_state *state)
{
	const struct tw_drm_connector_props *prop = state->props_connector;
	bool active = state->crtc.active;
	int32_t id = (active && prop) ?
		state->crtc_id : TW_DRM_CRTC_ID_INVALID;

	if (!prop || (active && id == TW_DRM_CRTC_ID_INVALID))
		return false;

	//we set the connector crtc based on if connector is active
	atomic_add(req, &pass, prop->id, prop->crtc_id, id);
	return pass;
}

static bool
tw_kms_atomic_set_crtc_active(drmModeAtomicReq *reg, bool pass,
                              struct tw_kms_state *state)
{
	const struct tw_drm_crtc_props *prop = state->props_crtc;
	bool active = state->crtc.active;

        if (!prop)
		return false;
        atomic_add(reg, &pass, prop->id, prop->active, active);
        return pass;
}

static bool
tw_kms_atomic_set_crtc_modeid(drmModeAtomicReq *req, bool pass,
                              struct tw_kms_state *state,
                              uint32_t pending_flags, int gpu_fd)
{
	const struct tw_drm_crtc_props *prop = state->props_crtc;
	const size_t mode_size = sizeof(drmModeModeInfo);
	bool pending_mode = (pending_flags & TW_DRM_PENDING_MODE);
	uint32_t mode_id = 0;

	if (!prop)
		return false;
	if (!pending_mode)
		return true;
	if (pending_mode && !state->crtc.active)
		return false;

	pass = pass && (drmModeCreatePropertyBlob(gpu_fd, &state->crtc.mode,
	                                          mode_size, &mode_id) == 0);
	atomic_add(req, &pass, prop->id, prop->mode_id, mode_id);
	if (!pass)
		drmModeDestroyPropertyBlob(gpu_fd, mode_id);
	state->crtc.mode_id = pass ? mode_id : 0;
	return pass;
}

bool
tw_kms_state_submit_atomic(struct tw_kms_state *state,
                           struct tw_drm_display *output, uint32_t flags)
{
	bool pass = true;
	drmModeAtomicReq *req = NULL;
	int gpu_fd = output->gpu->gpu_fd;
	uint32_t pending_flags = output->status.pending;

	if (pending_flags & TW_DRM_PENDING_MODE)
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	else
		flags |= DRM_MODE_ATOMIC_NONBLOCK;

	if (!(req = drmModeAtomicAlloc()))
		return pass;
	//TODO cursor plane and various other properties
	pass = tw_kms_atomic_set_plane_fb(req, pass, state);
	pass = tw_kms_atomic_set_connector_crtc(req, pass, state);
	pass = tw_kms_atomic_set_crtc_active(req, pass, state);
	pass = tw_kms_atomic_set_crtc_modeid(req, pass, state, pending_flags,
	                                     gpu_fd);

	pass = pass && (drmModeAtomicCommit(gpu_fd, req, flags, output) == 0);
	drmModeAtomicFree(req);
	output->status.pending = 0;
	return pass;
}

bool
tw_kms_state_submit_legacy(struct tw_kms_state *state,
                           struct tw_drm_display *output, uint32_t flags)
{
	int fd = output->gpu->gpu_fd;
	uint32_t crtc_id = state->props_crtc->id;
	const char *name = output->output.device.name;
	uint32_t pending_flags = output->status.pending;

	if (pending_flags & TW_DRM_PENDING_MODE) {
		uint32_t on = state->crtc.active ?
			DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;
		uint32_t conn_id = output->props.id;

		if (drmModeConnectorSetProperty(fd, output->props.id,
		                                output->props.dpms, on) != 0) {
			tw_logl_level(TW_LOG_ERRO, "Failed to set %s DPMS "
			              "property", name);
			return false;
		}
		if (drmModeSetCrtc(fd, crtc_id,
		                   state->fb.fb, state->fb.x, state->fb.y,
		                   &conn_id, 1, &state->crtc.mode) != 0) {
			tw_logl_level(TW_LOG_ERRO, "Failed to set %s CRTC",
			              name);
			return false;
		}
	}

	//TODO NO support for gamma and VRR yet.
	//TODO NO support for cursor plane yet.
	drmModeSetCursor(fd, crtc_id, 0, 0, 0);
	if (flags & DRM_MODE_PAGE_FLIP_EVENT) {
		if (drmModePageFlip(fd, crtc_id, state->fb.fb,
		                    DRM_MODE_PAGE_FLIP_EVENT, output) != 0) {
			tw_logl_level(TW_LOG_ERRO, "Failed to pageflip on %s",
			              name);
			return false;
		}
	}
	output->status.pending = 0;
	return true;
}

void
tw_kms_state_move(struct tw_kms_state *dst, struct tw_kms_state *src,
                  int drm_fd)
{
	dst->fb = src->fb;
	dst->props_connector = src->props_connector;
	dst->props_main_plane = src->props_main_plane;
	dst->props_crtc = src->crtc.active ? src->props_crtc : NULL;

	dst->crtc.active = src->crtc.active;
	dst->crtc.mode = src->crtc.mode;
	atomic_commit_prop_blob(0, &dst->crtc.mode_id, src->crtc.mode_id);
}

//how do we do a noop page_flip
void
tw_kms_state_duplicate(struct tw_kms_state *dst, struct tw_kms_state *src)
{
	dst->fb = src->fb;
	dst->crtc = src->crtc;
}

void
tw_kms_state_deactivate(struct tw_kms_state *state)
{
	const drmModeModeInfo none_mode = {0};

	plane_fb_init(&state->fb);
	state->crtc_id = TW_DRM_CRTC_ID_INVALID;
	state->crtc.mode = none_mode;
	state->crtc.active = false;
	state->crtc.mode_id = 0;
}
