/*
 * backend.c - taiwins server drm KMS functions
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

bool
tw_kms_atomic_set_plane_props(drmModeAtomicReq *req, bool pass,
                              struct tw_drm_plane *plane,
                              uint32_t crtc_id,
                              int x, int y, int w, int h)
{
	uint32_t id = plane->id;
	struct tw_drm_plane_props *props = &plane->props;
	struct tw_drm_fb *fb = &plane->pending;

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
	return pass;
}

bool
tw_kms_atomic_set_connector_props(drmModeAtomicReq *req, bool pass,
                                  struct tw_drm_display *output,
                                  uint32_t crtc)
{
	atomic_add(req, &pass, output->conn_id, output->props.crtc_id, crtc);
	return pass;
}
