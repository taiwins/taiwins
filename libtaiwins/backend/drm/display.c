/*
 * backend.c - taiwins server drm display functions
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/render_output.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xf86drmMode.h>

#include "internal.h"

static inline enum wl_output_subpixel
wl_subpixel_from_drm(drmModeSubPixel drm_subpixel)
{
	switch (drm_subpixel) {
	case DRM_MODE_SUBPIXEL_UNKNOWN:
		return WL_OUTPUT_SUBPIXEL_UNKNOWN;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
	case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
	case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
	case DRM_MODE_SUBPIXEL_NONE:
	default:
		return WL_OUTPUT_SUBPIXEL_NONE;
	}
}

static inline const char *
name_from_type(uint32_t connector_type)
{
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_Unknown:
		return "Unknown";
	case DRM_MODE_CONNECTOR_VGA:
		return "VGA";
	case DRM_MODE_CONNECTOR_DVII:
		return "DVI-I";
	case DRM_MODE_CONNECTOR_DVID:
		return "DVI-D";
	case DRM_MODE_CONNECTOR_DVIA:
		return "DVI-A";
	case DRM_MODE_CONNECTOR_Composite:
		return "Composite";
	case DRM_MODE_CONNECTOR_SVIDEO:
		return "SVIDEO";
	case DRM_MODE_CONNECTOR_LVDS:
		return "LVDS";
	case DRM_MODE_CONNECTOR_Component:
		return "Component";
	case DRM_MODE_CONNECTOR_9PinDIN:
		return "DIN";
	case DRM_MODE_CONNECTOR_DisplayPort:
		return "DP";
	case DRM_MODE_CONNECTOR_HDMIA:
		return "HDMI-A";
	case DRM_MODE_CONNECTOR_HDMIB:
		return "HDMI-B";
	case DRM_MODE_CONNECTOR_TV:
		return "TV";
	case DRM_MODE_CONNECTOR_eDP:
		return "eDP";
	case DRM_MODE_CONNECTOR_VIRTUAL:
		return "Virtual";
	case DRM_MODE_CONNECTOR_DSI:
		return "DSI";
#ifdef DRM_MODE_CONNECTOR_DPI
	case DRM_MODE_CONNECTOR_DPI:
		return "DPI";
#endif
	default:
		return "Unknown";
	}
}

static uint32_t
read_connector_possible_crtcs(int fd, drmModeConnector *conn)
{
	uint32_t possible_crtcs = 0;

	for (int i = 0; i < conn->count_encoders; i++) {
		drmModeEncoder *encoder =
			drmModeGetEncoder(fd, conn->encoders[i]);
		if (!encoder)
			continue;
		possible_crtcs |= encoder->possible_crtcs;
		drmModeFreeEncoder(encoder);
	}
	return possible_crtcs;
}

static inline void
tw_drm_display_attach_crtc(struct tw_drm_display *display,
                           struct tw_drm_crtc *crtc)
{
	display->crtc = crtc;
	display->status.crtc_id = crtc->id;
	display->crtc->display = display;
}

static inline void
tw_drm_display_detach_crtc(struct tw_drm_display *display)
{
	struct tw_drm_crtc *crtc = display->crtc;

	if (crtc)
		crtc->display = NULL;
	display->crtc = NULL;
	display->status.crtc_id = -1;
}

static bool
tw_drm_display_read_info(struct tw_drm_display *output, drmModeConnector *conn)
{
	bool ret = true;
	int crtc_id;
	int fd = output->gpu->gpu_fd;
	drmModeEncoder *encoder;
	drmModeCrtc *crtc;
	struct tw_output_device *dev = &output->output.device;

	encoder = drmModeGetEncoder(fd, conn->encoder_id);
	if (encoder != NULL) {
                crtc = drmModeGetCrtc(fd, encoder->crtc_id);
                crtc_id = encoder->crtc_id;
                drmModeFreeEncoder(encoder);

                if (crtc == NULL) {
	                ret = false;
	                output->status.crtc_id = -1;
	                goto out;
                } else {
	                output->status.crtc_id = crtc_id;
	                output->status.inherited_mode.w = crtc->mode.hdisplay;
	                output->status.inherited_mode.h = crtc->mode.vdisplay;
	                output->status.inherited_mode.refresh =
		                crtc->mode.vrefresh;
	                drmModeFreeCrtc(crtc);
                }
	} else {
		output->status.crtc_id = -1;
	}
out:
	output->crtc_mask = read_connector_possible_crtcs(fd, conn);
	output->conn_id = conn->connector_id;
	output->status.connected = conn->connection == DRM_MODE_CONNECTED;
	//read modes
	wl_array_release(&dev->available_modes);
	wl_array_init(&dev->available_modes);
	for (int i = 0; i < conn->count_modes; i++) {
		drmModeModeInfo *conn_mode = &conn->modes[i];
		struct tw_output_device_mode *dev_mode =
			wl_array_add(&dev->available_modes, sizeof(*dev_mode));
		if (dev_mode) {
			dev_mode->w = conn_mode->hdisplay;
			dev_mode->h = conn_mode->vdisplay;
			dev_mode->refresh = conn_mode->vrefresh;
		};
	}
	dev->phys_width = conn->mmWidth;
	dev->phys_height = conn->mmHeight;
	dev->subpixel = wl_subpixel_from_drm(conn->subpixel);
	snprintf(dev->name, sizeof(dev->name), "%s-%d",
	         name_from_type(conn->connector_type), conn->connector_id);
	//TODO: parsing edid from connector properities so we can fill up the
	//make, model

	return ret;

}

struct tw_drm_display *
tw_drm_display_find_create(struct tw_drm_gpu *gpu, drmModeConnector *conn)
{
	struct tw_drm_display *c, *found = NULL;
	struct tw_drm_backend *drm = gpu->drm;

	wl_list_for_each(c, &drm->base.outputs, output.device.link) {
		if (c->conn_id == (int)conn->connector_id) {
			found = c;
			return found;
		}
	}
	if (!found) {
		found = calloc(1, sizeof(*found));
		if (!found)
			return NULL;
		tw_render_output_init(&found->output, NULL);
		found->drm = drm;
		found->gpu = gpu;

		if (!tw_drm_display_read_info(found, conn))
			tw_logl_level(TW_LOG_WARN, "failed to read current"
			              " mode from output");

		wl_list_insert(drm->base.outputs.prev,
		               &found->output.device.link);

	}
	return found;
}

static inline int
crtc_idx_from_id(struct tw_drm_gpu *gpu, int id)
{
	for (int i = 0; i < 32; i++)
		if (((1 << i) & gpu->crtc_mask) && id == gpu->crtcs[i].id)
			return i;
	assert(0);
	return -1;
}

static inline struct tw_drm_crtc *
crtc_from_id(struct tw_drm_gpu *gpu, int id)
{
	for (int i = 0; i < 32; i++)
		if (((1 << i) & gpu->crtc_mask) && id == gpu->crtcs[i].id)
			return &gpu->crtcs[i];
	return NULL;
}

static inline struct tw_drm_plane *
find_plane(struct tw_drm_display *output, enum tw_drm_plane_type t)
{
	struct tw_drm_plane *p;
	struct tw_drm_gpu *gpu = output->gpu;
	int crtc_idx = crtc_idx_from_id(gpu, output->status.crtc_id);

        wl_list_for_each(p, &gpu->plane_list, base.link)
		if (((1 << crtc_idx) & p->crtc_mask) && p->type == t)
			return p;
        return NULL;
}

static bool
find_display_crtc(struct tw_drm_display *output)
{
	struct tw_drm_gpu *gpu = output->gpu;
	struct tw_drm_crtc *crtc, *potential_crtc =
		crtc_from_id(gpu, output->status.crtc_id);
	uint32_t mask = output->crtc_mask;
	//not attached
	if (potential_crtc && (!potential_crtc->display)) {
		tw_drm_display_attach_crtc(output, potential_crtc);
		return true;
	} else {
		wl_list_for_each(crtc, &gpu->crtc_list, link) {
			//compatible and not taken
			if (((1 << crtc->idx) & mask) && !crtc->display) {
				tw_drm_display_attach_crtc(output, crtc);
				return true;
			}
		}
	}
	return false;
}

void
tw_drm_display_start(struct tw_drm_display *output)
{
	struct tw_drm_backend *drm = output->drm;

	//this may not work
	if (!find_display_crtc(output)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to find a crtc driving "
		              "output:%s", output->output.device.name);
		return;
	}

	tw_render_output_set_context(&output->output, drm->base.ctx);
	//TODO: setting the current resolution, this reminds me that maybe we
	//should have signal the output first
	/* tw_output_device_commit_state(&output->output.device); */

	output->primary_plane = find_plane(output, TW_DRM_PLANE_MAJOR);

	tw_drm_display_start_gbm(output);

	wl_signal_emit(&drm->base.events.new_output, &output->output.device);
}

void
tw_drm_display_remove(struct tw_drm_display *output)
{
	tw_drm_display_fini_gbm(output);
	free(output);
}
