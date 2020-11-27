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
#include <wayland-server.h>
#include <xf86drmMode.h>

#include "internal.h"

/******************************************************************************
 * pending
 *****************************************************************************/

#define UPDATE_PENDING(dpy, name, newval, flags) \
	dpy->status.pending |= (dpy->status.name != newval) ? flags : 0; \
	dpy->status.name = newval;

static inline void
UPDATE_PENDING_MODE(struct tw_drm_display *dpy, drmModeModeInfo *new_mode)
{
	if (new_mode) {
		bool nequal = memcmp(new_mode, &dpy->status.mode,
		                     sizeof(*new_mode));
		dpy->status.pending |= nequal ? TW_DRM_PENDING_MODE : 0;
		dpy->status.mode = *new_mode;
	}
}

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

static inline bool
tw_drm_display_attach_crtc(struct tw_drm_display *display,
                           struct tw_drm_crtc *crtc)
{
	if (crtc->display && crtc->display != display)
		return false;
	display->crtc = crtc;
	UPDATE_PENDING(display, crtc_id, crtc->id, TW_DRM_PENDING_CRTC);
	display->crtc->display = display;
	return true;
}

static inline void
tw_drm_display_detach_crtc(struct tw_drm_display *display)
{
	struct tw_drm_crtc *crtc = display->crtc;
	const int id = TW_DRM_CRTC_ID_INVALID;

	if (crtc)
		crtc->display = NULL;
	display->crtc = NULL;
	UPDATE_PENDING(display, crtc_id, id, TW_DRM_PENDING_CRTC);
	UPDATE_PENDING(display, active, false, TW_DRM_PENDING_ACTIVE);
	display->status.crtc_id = TW_DRM_CRTC_ID_INVALID;
}

static bool
read_display_modes(struct tw_drm_display *output, drmModeConnector *conn)
{
	size_t n = conn->count_modes;
	size_t s_mode_info = n * sizeof(struct tw_drm_mode_info);
	struct tw_drm_mode_info *mode_info = NULL;
	struct tw_output_device *dev = &output->output.device;

	wl_array_release(&output->status.modes);
	wl_array_init(&output->status.modes);
	wl_list_init(&dev->mode_list);

	if (!wl_array_add(&output->status.modes, s_mode_info))
		goto err;

	mode_info = output->status.modes.data;
	for (int i = 0; i < conn->count_modes; i++) {
		drmModeModeInfo *mode = &conn->modes[i];
		mode_info[i].info = *mode;
		mode_info[i].mode.w = mode->hdisplay;
		mode_info[i].mode.h = mode->vdisplay;
		mode_info[i].mode.refresh = mode->vrefresh;
		mode_info[i].mode.preferred =
			mode->type & DRM_MODE_TYPE_PREFERRED;
		wl_list_init(&mode_info[i].mode.link);
		wl_list_insert(dev->mode_list.prev, &mode_info[i].mode.link);
	}
	return true;
err:
	wl_array_release(&output->status.modes);
	wl_array_init(&output->status.modes);
	wl_list_init(&dev->mode_list);
	return false;
}

static inline void
read_display_props(struct tw_drm_display *output, int fd)
{
	struct tw_drm_prop_info prop_info[] = {
		{"CRTC_ID", &output->props.crtc_id},
		{"DPMS", &output->props.dpms},
		{"EDID", &output->props.edid},
	};
	tw_drm_read_properties(fd, output->conn_id, DRM_MODE_OBJECT_CONNECTOR,
	                       prop_info,
	                       sizeof(prop_info)/sizeof(prop_info[0]));
}

/******************************************************************************
 * display commit
 *****************************************************************************/

static inline bool
display_enable(struct tw_drm_display *output)
{
	return output->status.active && output->status.connected;
}

static inline int
crtc_idx_from_id(struct tw_drm_gpu *gpu, int id)
{
	for (int i = 0; i < 32; i++)
		if (((1 << i) & gpu->crtc_mask) && id == gpu->crtcs[i].id)
			return i;
	assert(0);
	return TW_DRM_CRTC_ID_INVALID;
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

static void
handle_display_commit_state(struct tw_output_device *device)
{
	struct tw_drm_display *output =
		wl_container_of(device, output, output.device);
	struct tw_output_device_state *pending = &device->pending;
	struct tw_output_device_mode *mode =
		tw_output_device_match_mode(device,
		                            pending->current_mode.w,
		                            pending->current_mode.h,
		                            pending->current_mode.refresh);
	struct tw_drm_mode_info *mode_info = (mode) ?
		wl_container_of(mode, mode_info, mode) : NULL;
	drmModeModeInfo *info = mode_info ? &mode_info->info : NULL;
	uint32_t flags = 0;

	//ensure valid active state.
	pending->enabled = pending->enabled && output->status.connected;
	//update the pending.
	UPDATE_PENDING(output, active,pending->enabled, TW_DRM_PENDING_ACTIVE);
	UPDATE_PENDING_MODE(output, info);
	memcpy(&device->state, &device->pending, sizeof(device->state));

	//flushing the pending states
	if (display_enable(output)) {
		if (!find_display_crtc(output)) {
			tw_logl_level(TW_LOG_ERRO, "Failed to find a crtc for "
			              "output:%s", output->output.device.name);
			return;
		}
		output->primary_plane = find_plane(output, TW_DRM_PLANE_MAJOR);

		if ((output->status.pending & TW_DRM_PENDING_MODE))
			output->gpu->impl->allocate_fb(output);
		flags = DRM_MODE_PAGE_FLIP_EVENT;
	} else {
		tw_drm_display_detach_crtc(output);
		output->gpu->impl->end_display(output);
		flags = 0;
	}
	output->gpu->impl->page_flip(output, flags);
}

static const struct tw_output_device_impl output_dev_impl = {
	handle_display_commit_state,
};

/******************************************************************************
 * output API
 *****************************************************************************/

bool
tw_drm_display_read_info(struct tw_drm_display *output, drmModeConnector *conn)
{
	bool ret = true;
	int crtc_id;
	bool connected, enabled;
	int fd = output->gpu->gpu_fd;
	drmModeEncoder *encoder = NULL;
	drmModeCrtc *crtc = NULL;
	drmModeModeInfo mode = {0};
	struct tw_output_device *dev = &output->output.device;

	//we will be update display status and setup the pending.
	encoder = drmModeGetEncoder(fd, conn->encoder_id);
	if (encoder != NULL) {
                crtc = drmModeGetCrtc(fd, encoder->crtc_id);
                crtc_id = encoder->crtc_id;
                drmModeFreeEncoder(encoder);

                if (crtc == NULL) {
	                ret = false;
	                crtc_id = TW_DRM_CRTC_ID_INVALID;
	                goto out;
                } else {
	                mode = crtc->mode;
	                drmModeFreeCrtc(crtc);
                }
	} else {
		crtc_id = TW_DRM_CRTC_ID_INVALID;
	}
out:
	read_display_props(output, fd);
	read_display_modes(output, conn);
	dev->phys_width = conn->mmWidth;
	dev->phys_height = conn->mmHeight;
	dev->subpixel = wl_subpixel_from_drm(conn->subpixel);
	snprintf(dev->name, sizeof(dev->name), "%s-%d",
	         name_from_type(conn->connector_type), conn->connector_id);
	output->crtc_mask = read_connector_possible_crtcs(fd, conn);
	output->conn_id = conn->connector_id;
	output->crtc = NULL;
	//TODO: parsing edid from connector properities so we can fill up the
	//make, model

	//// update pending status
	connected = conn->connection == DRM_MODE_CONNECTED;
	enabled = dev->pending.enabled && connected;
	output->status.mode = mode; //note that if connector does not have a
				    //crtc, it does not have mode either
	UPDATE_PENDING(output, connected, connected, TW_DRM_PENDING_CONNECT);
	UPDATE_PENDING(output, active, enabled, TW_DRM_PENDING_ACTIVE);
	UPDATE_PENDING(output, crtc_id, crtc_id, TW_DRM_PENDING_CRTC);

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
		tw_render_output_init(&found->output, &output_dev_impl);
		found->drm = drm;
		found->gpu = gpu;
		found->conn_id = conn->connector_id;

		wl_list_insert(drm->base.outputs.prev,
		               &found->output.device.link);
		return found;
	}
	return found;
}

void
tw_drm_display_start(struct tw_drm_display *output)
{
	struct tw_drm_backend *drm = output->drm;

	tw_render_output_set_context(&output->output, drm->base.ctx);

	//TODO: avoid call this twice!
	wl_signal_emit(&drm->base.events.new_output, &output->output.device);

	//commit state would now handle most of the logics
	tw_output_device_commit_state(&output->output.device);
}

void
tw_drm_display_continue(struct tw_drm_display *output)
{
	tw_output_device_commit_state(&output->output.device);
}

void
tw_drm_display_stop(struct tw_drm_display *output)
{
	tw_output_device_commit_state(&output->output.device);
}

void
tw_drm_display_remove(struct tw_drm_display *output)
{
	UPDATE_PENDING(output, connected, false, TW_DRM_PENDING_CONNECT);
	tw_drm_display_stop(output);
	wl_array_release(&output->status.modes);
	tw_render_output_fini(&output->output);
	free(output);
}

void
tw_drm_display_check_start_stop(struct tw_drm_display *output,
                                bool *need_start, bool *need_stop,
                                bool *need_continue)
{
	struct tw_drm_backend *drm = output->drm;
	bool connect_change, active_change;
	bool pending_connect, pending_disconnect;
	bool pending_activate, pending_deactivate;
	bool backend_started;

	connect_change = (output->status.pending & TW_DRM_PENDING_CONNECT);
	active_change = (output->status.pending & TW_DRM_PENDING_ACTIVE);

	pending_connect = (output->status.connected && connect_change);
	pending_disconnect = (!output->status.connected && connect_change);

	pending_activate = (output->status.active && active_change);
	pending_deactivate = (!output->status.active && active_change);

	//need_find_crtc happens after recollecting resources, all the display
	//loses its crtc.
	backend_started = drm->base.started;
	if (need_continue)
		*need_continue = (output->status.active &&
		                  output->status.connected &&
		                  !output->crtc && backend_started);
	if (need_start)
		*need_start = (pending_connect || pending_activate) &&
			backend_started;
	if (need_stop)
		*need_stop = (pending_disconnect || pending_deactivate) &&
			backend_started;
}
