/*
 * backend.c - taiwins server drm display kms functions
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

/******************************************************************************
 * Atomic Modesetting
 *****************************************************************************/
static inline void
atomic_add(drmModeAtomicReq *req, bool *pass, uint32_t id, uint32_t prop,
           uint64_t val)
{
	if (pass) {
		int ret = drmModeAtomicAddProperty(req, id, prop, val);
		*pass = *pass && (ret >= 0);
	}
}

static bool
set_plane_properties(drmModeAtomicReq *req, bool pass,
                     struct tw_drm_plane *plane, uint32_t crtc_id,
                     int x, int y, int w, int h)
{
	uint32_t id = plane->id;
	struct tw_drm_plane_props *props = &plane->props;
	struct tw_drm_fb *fb = &plane->pending;
	int fb_id = tw_drm_gbm_get_fb(fb->gbm.bo);

	atomic_add(req, &pass, id, props->src_x, 0);
	atomic_add(req, &pass, id, props->src_y, 0);
	atomic_add(req, &pass, id, props->src_w, (uint64_t)w << 16);
	atomic_add(req, &pass, id, props->src_h, (uint64_t)h << 16);
	atomic_add(req, &pass, id, props->crtc_x, x);
	atomic_add(req, &pass, id, props->crtc_y, y);
	atomic_add(req, &pass, id, props->crtc_w, w);
	atomic_add(req, &pass, id, props->crtc_h, h);
	atomic_add(req, &pass, id, props->crtc_id, crtc_id);
	atomic_add(req, &pass, id, props->fb_id, fb_id);
	return pass;
}

void
tw_drm_display_atomic_pageflip(struct tw_drm_display *output)
{
	bool pass = true;
	struct tw_output_device *dev = &output->output.device;
	struct gbm_bo *next_bo = NULL;
	int fd = output->gpu->gpu_fd;
	unsigned w, h;
	struct tw_drm_plane *main_plane = output->primary_plane;
	struct tw_drm_crtc *crtc = output->crtc;
	drmModeAtomicReq *req = NULL;
	/* uint32_t mode_id; //you don't need the mode_id actually */

	//TODO: this is totally not correct, we are facing different commit, as
	//we could face different commit event. Here we operate like we just do
	//page flip.
	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	/* drmModeCreatePropertyBlob(fd, &output->status.mode, */
	/*                           sizeof(output->status.mode), &mode_id); */
	//TODO: we only handle main plane for now.
	if (!main_plane)
		return;
	tw_drm_fb_release(&main_plane->pending);

	req = drmModeAtomicAlloc();
	if (!req)
		return;

	//we do the drawing for now We could have been commit from here.
	//we should actually do this in the commit
	wl_signal_emit(&dev->events.new_frame, dev);

	//TODO: next_bo need to be freed by gbm_surface_free_buffer.
	next_bo = gbm_surface_lock_front_buffer(output->gbm_surface.gbm);
	if (!next_bo) {
		tw_logl_level(TW_LOG_ERRO, "Failed to lock front buffer");
		return;
	}
	w = gbm_bo_get_width(next_bo);
	h = gbm_bo_get_height(next_bo);
	main_plane->pending.gbm.bo = next_bo;
	main_plane->pending.gbm.surf = output->gbm_surface.gbm;
	atomic_add(req, &pass, output->conn_id, output->props.crtc_id,
	           crtc->id);
	atomic_add(req, &pass, crtc->id, crtc->props.active, true);
	/* atomic_add(req, &pass, crtc->id, crtc->props.mode_id, mode_id); */
	set_plane_properties(req, pass, main_plane, crtc->id, 0, 0, w, h);

	drmModeAtomicCommit(fd, req, flags, output->gpu);
	drmModeAtomicFree(req);
	/* drmModeDestroyPropertyBlob(fd, mode_id); */

	//TODO swap the fbs
	tw_drm_plane_commit_fb(main_plane);
}


void
tw_drm_display_legacy_pageflip(struct tw_drm_display *display)
{

}
