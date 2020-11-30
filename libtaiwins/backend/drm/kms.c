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
                     struct tw_drm_plane *plane)
{
	if (plane) {
		struct tw_drm_plane_props *props = &plane->props;

		atomic_add(req, pass, plane->id, props->crtc_id, 0);
		atomic_add(req, pass, plane->id, props->fb_id, 0);
	}
}

bool
tw_kms_atomic_set_plane_props(drmModeAtomicReq *req, bool pass,
                              struct tw_drm_plane *plane,
                              int crtc_id, int x, int y, int w, int h)
{

	if (crtc_id == TW_DRM_CRTC_ID_INVALID) {
		atomic_plane_disable(req, &pass, plane);
	} else {
		uint32_t id = plane->id;
		struct tw_drm_plane_props *props = &plane->props;
		struct tw_drm_fb *fb = plane->pending;

		atomic_add(req, &pass, id, props->src_x, 0);
		atomic_add(req, &pass, id, props->src_y, 0);
		atomic_add(req, &pass, id, props->src_w, (uint64_t)w << 16);
		atomic_add(req, &pass, id, props->src_h, (uint64_t)h << 16);
		atomic_add(req, &pass, id, props->crtc_x, x);
		atomic_add(req, &pass, id, props->crtc_y, y);
		atomic_add(req, &pass, id, props->crtc_w, w);
		atomic_add(req, &pass, id, props->crtc_h, h);
		atomic_add(req, &pass, id, props->crtc_id, crtc_id);
		atomic_add(req, &pass, id, props->fb_id, fb->fb);
	}
	return pass;
}

bool
tw_kms_atomic_set_connector_props(drmModeAtomicReq *req, bool pass,
                                  struct tw_drm_display *output)

{
	int32_t crtc = output->status.crtc_id;

	assert((output->crtc != NULL) == (crtc != TW_DRM_CRTC_ID_INVALID));
	atomic_add(req, &pass, output->conn_id, output->props.crtc_id, crtc);
	return pass;
}

bool
tw_kms_atomic_set_crtc_props(drmModeAtomicReq *req, bool pass,
                             struct tw_drm_display *output,
                             //return values
                             uint32_t *modeid)
{
	int fd = output->gpu->gpu_fd;
	bool active = output->status.active;
	struct tw_drm_crtc *crtc = output->crtc ?
		output->crtc : output->status.unset_crtc;
	const size_t mode_size = sizeof(drmModeModeInfo);

	assert(active == (output->crtc != NULL));
	if (!active) {
		*modeid = 0;
		goto out;
	}
	if (output->status.pending & TW_DRM_PENDING_MODE) {
		pass = pass && drmModeCreatePropertyBlob(fd,
		                                         &output->status.mode,
		                                         mode_size,
		                                         modeid) >= 0;
		atomic_add(req, &pass, crtc->id, crtc->props.mode_id, *modeid);
	}
out:
	if (crtc)
		atomic_add(req, &pass, crtc->id, crtc->props.active, active);

	return pass;
}
