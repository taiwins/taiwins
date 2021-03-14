/*
 * display.c - taiwins server drm display functions
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

bool
drm_display_read_edid(int fd, drmModeConnector *conn, uint32_t prop_edid,
                      char make[32], char model[32], char serial[16]);
/******************************************************************************
 * pending
 *****************************************************************************/

#define UPDATE_PENDING(dpy, name, val, flg) \
	dpy->status.flags |= (dpy->status.kms_current.name != val) ? flg : 0; \
	dpy->status.kms_pending.name = val;

static inline void
UPDATE_PENDING_MODE(struct tw_drm_display *dpy, drmModeModeInfo *next,
                    bool force)
{
	drmModeModeInfo *curr = &dpy->status.kms_current.crtc.mode;
	drmModeModeInfo *pend = &dpy->status.kms_pending.crtc.mode;
	bool nequal = next ? memcmp(next, curr, sizeof(*next)) : false;

	dpy->status.flags |= (nequal || force) ? TW_DRM_PENDING_MODE : 0;
	if (next)
		*pend = *next;
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

static int
read_connector_curr_crtc(int fd, drmModeConnector *conn)
{
	int crtc_id = TW_DRM_CRTC_ID_INVALID;
	drmModeEncoder *encoder = drmModeGetEncoder(fd, conn->encoder_id);

	if (encoder != NULL) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, encoder->crtc_id);

		if (crtc) {
			drmModeFreeCrtc(crtc);
			crtc_id = encoder->crtc_id;
		}
		drmModeFreeEncoder(encoder);
	}
	return crtc_id;
}

static inline bool
tw_drm_display_attach_crtc(struct tw_drm_display *display,
                           struct tw_drm_crtc *crtc)
{
	if (crtc->display && crtc->display != display)
		return false;
	display->crtc = crtc;
	display->crtc->display = display;
	//updating pending kms
	display->status.kms_pending.props_crtc = &crtc->props;
	UPDATE_PENDING(display, crtc_id, crtc->props.id, TW_DRM_PENDING_CRTC);

	return true;
}

static inline void
tw_drm_display_detach_crtc(struct tw_drm_display *display)
{
	struct tw_drm_crtc *crtc = display->crtc;

	if (crtc)
		crtc->display = NULL;
	display->crtc = NULL;
	UPDATE_PENDING(display, crtc.active, false, TW_DRM_PENDING_ACTIVE);
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
		mode_info[i].mode.refresh = mode->vrefresh * 1000; /* mHz */
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
read_display_props(struct tw_drm_display *output, drmModeConnector *conn,
                   int fd)
{
	struct tw_drm_prop_info prop_info[] = {
		{"CRTC_ID", &output->props.crtc_id},
		{"DPMS", &output->props.dpms},
		{"EDID", &output->props.edid},
	};

	output->props.id = conn->connector_id;
	tw_drm_read_properties(fd, conn->connector_id,
	                       DRM_MODE_OBJECT_CONNECTOR,
	                       prop_info,
	                       sizeof(prop_info)/sizeof(prop_info[0]));
}

/******************************************************************************
 * display commit
 *****************************************************************************/

static inline bool
pending_enable(struct tw_drm_display *output)
{
	return output->status.kms_pending.crtc.active;
}

static inline int
crtc_idx_from_id(struct tw_drm_gpu *gpu, int id)
{
	for (int i = 0; i < 32; i++)
		if (((1<<i) & gpu->crtc_mask) && id == gpu->crtcs[i].props.id)
			return i;
	assert(0);
	return TW_DRM_CRTC_ID_INVALID;
}

static inline struct tw_drm_crtc *
crtc_from_id(struct tw_drm_gpu *gpu, int id)
{
	for (int i = 0; i < 32; i++)
		if (((1<<i) & gpu->crtc_mask) && id == gpu->crtcs[i].props.id)
			return &gpu->crtcs[i];
	return NULL;
}

static inline struct tw_drm_plane *
find_plane(struct tw_drm_display *dpy, enum tw_drm_plane_type t, int crtc_id)
{
	struct tw_drm_plane *p;
	struct tw_drm_gpu *gpu = dpy->gpu;
	int crtc_idx = crtc_idx_from_id(gpu, crtc_id);

	wl_list_for_each(p, &gpu->plane_list, base.link)
	        if (((1 << crtc_idx) & p->crtc_mask) && p->type == t)
		        return p;
	return NULL;
}

static int
find_display_crtc(struct tw_drm_display *output)
{
	uint32_t mask = output->crtc_mask;

	struct tw_drm_gpu *gpu = output->gpu;
	struct tw_drm_crtc *crtc = NULL;
	/* struct tw_drm_crtc *potential_crtc = */
	/*	crtc_from_id(gpu, output->status.crtc_id); */
	/* //not attached */
	/* if (potential_crtc && (!potential_crtc->display)) { */
	/*	tw_drm_display_attach_crtc(output, potential_crtc); */
	/*	return true; */
	/* } else { */
	wl_list_for_each(crtc, &gpu->crtc_list, link) {
		//compatible and not taken
		if (((1 << crtc->idx) & mask) && !crtc->display) {
			tw_drm_display_attach_crtc(output, crtc);
			return crtc->props.id;
		}
	}
	/* } */
	return TW_DRM_CRTC_ID_INVALID;
}

/* handles the selection of display mode from available candidates, NOTE
 * that if display is not connected, nothing happens here */
static inline void
select_display_mode(struct tw_drm_display *output)
{
	struct tw_output_device *device = &output->output.device;
	struct tw_output_device_state *pending = &device->pending;
	struct tw_output_device_mode *mode =
		tw_output_device_match_mode(device,
		                            pending->current_mode.w,
		                            pending->current_mode.h,
		                            pending->current_mode.refresh);
	struct tw_drm_mode_info *mode_info = (mode) ?
		wl_container_of(mode, mode_info, mode) : NULL;
	drmModeModeInfo *info = mode_info ? &mode_info->info : NULL;
	UPDATE_PENDING_MODE(output, info, false);
	if (mode_info)
		tw_output_device_set_mode(device, &mode_info->mode);
}

static void
handle_display_commit_state(struct tw_output_device *device)
{
	struct tw_drm_display *output =
		wl_container_of(device, output, output.device);
	//ensure valid active state.
	int crtc;
	bool enabled = device->pending.enabled && pending_enable(output);

	select_display_mode(output);
	UPDATE_PENDING(output, crtc.active, enabled, TW_DRM_PENDING_ACTIVE);
	memcpy(&device->state, &device->pending, sizeof(device->state));
	//we cannot use output_device_enable hear because it sets the pending
	device->state.enabled = enabled;

	//flushing the pending states
	if (pending_enable(output)) {
		if ((crtc = find_display_crtc(output)) ==
		    TW_DRM_CRTC_ID_INVALID) {
			tw_logl_level(TW_LOG_ERRO, "Failed to find a crtc for "
			              "output:%s", output->output.device.name);
			return;
		}
		output->primary_plane =
			find_plane(output, TW_DRM_PLANE_MAJOR, crtc);

		if ((output->status.flags & TW_DRM_PENDING_MODE))
			output->gpu->impl->allocate_fb(output,
			                               &output->status.kms_pending.crtc.mode);
		wl_signal_emit(&device->signals.new_frame, device);
	} else {
		tw_drm_display_detach_crtc(output);
		output->gpu->impl->end_display(output);
		//the pageflip should turn off display
		output->gpu->impl->page_flip(output, 0);

	}
}

static const struct tw_output_device_impl output_dev_impl = {
	handle_display_commit_state,
};

static void
notify_display_presentable_commit(struct wl_listener *listener, void *data)
{
	struct tw_drm_display *output =
		wl_container_of(listener, output, presentable_commit);
	struct tw_kms_state *pending_state =  &output->status.kms_pending;

	assert(data == &output->output.surface);
	if (output->gpu->impl->compose_fb(output, pending_state)) {

	}

	output->gpu->impl->page_flip(output, DRM_MODE_PAGE_FLIP_EVENT);
}

/******************************************************************************
 * output API
 *****************************************************************************/

bool
tw_drm_display_read_info(struct tw_drm_display *output, drmModeConnector *conn)
{
	int fd = output->gpu->gpu_fd;
	//drmModeModeInfo mode = {0};
	struct tw_output_device *dev = &output->output.device;
	int crtc_id = read_connector_curr_crtc(fd, conn);
	(void)crtc_id; //TODO use this later

	//setup static data
	read_display_props(output, conn, fd);
	read_display_modes(output, conn);
	dev->phys_width = conn->mmWidth;
	dev->phys_height = conn->mmHeight;
	dev->subpixel = wl_subpixel_from_drm(conn->subpixel);
	snprintf(dev->name, sizeof(dev->name), "%s-%d",
	         name_from_type(conn->connector_type),
	         conn->connector_type_id);
	drm_display_read_edid(fd, conn, output->props.edid,
	                      output->output.device.make,
	                      output->output.device.model,
	                      output->output.device.serial);
	output->crtc_mask = read_connector_possible_crtcs(fd, conn);
	output->crtc = NULL;
	//make, model

	return true;
}

struct tw_drm_display *
tw_drm_display_find_create(struct tw_drm_gpu *gpu, drmModeConnector *conn)
{
	struct tw_drm_display *c, *found = NULL;
	struct tw_drm_backend *drm = gpu->drm;

	wl_list_for_each(c, &drm->base.outputs, output.device.link) {
		if (c->props.id == (int)conn->connector_id) {
			found = c;
			return found;
		}
	}
	//only collect connected display
	if (!found && conn->connection == DRM_MODE_CONNECTED) {
		found = calloc(1, sizeof(*found));
		if (!found)
			return NULL;
		tw_render_output_init(&found->output, &output_dev_impl);
		found->drm = drm;
		found->gpu = gpu;

		wl_list_init(&found->presentable_commit.link);
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

	wl_signal_emit(&drm->base.signals.new_output,
	               &output->output.device);

	tw_output_device_commit_state(&output->output.device);

	//TODO setup a new listener for output_state here, it is how we can
	//apply the output mode?

	tw_render_output_set_context(&output->output, drm->base.ctx);
	tw_signal_setup_listener(&output->output.surface.commit,
	                         &output->presentable_commit,
	                         notify_display_presentable_commit);
	output->output.state.enabled = true;
	tw_render_output_dirty(&output->output);

}

void
tw_drm_display_continue(struct tw_drm_display *output)
{
	/* if (output->status.connected) { */
	output->output.state.enabled = true;
	tw_output_device_commit_state(&output->output.device);
	tw_render_output_dirty(&output->output);
	/* } */
}

void
tw_drm_display_pause(struct tw_drm_display *output)
{
	output->output.state.enabled = false;
	tw_drm_display_detach_crtc(output);
	output->gpu->impl->page_flip(output, 0);
}

void
tw_drm_display_stop(struct tw_drm_display *output)
{
	tw_reset_wl_list(&output->presentable_commit.link);
	output->output.state.enabled = false;
	tw_output_device_commit_state(&output->output.device);
	if (output->output.ctx)
		tw_render_output_unset_context(&output->output);
	output->status.unset_crtc = NULL;
}

void
tw_drm_display_remove(struct tw_drm_display *output)
{
	tw_drm_display_stop(output);
	wl_array_release(&output->status.modes);
	tw_render_output_fini(&output->output);
	free(output);
}

void
tw_drm_display_check_action(struct tw_drm_display *output,
                            drmModeConnector *conn,
                            bool *need_start, bool *need_stop,
                            bool *need_continue, bool *need_remove)
{
	struct tw_drm_backend *drm = output->drm;
	struct tw_output_device *dev = &output->output.device;
	struct tw_kms_state *state_curr = &output->status.kms_current;
	//We have a big problem here as user haven't setup pending yet, it is
	//the default pending,
	struct tw_output_device_state *user_pending = &dev->pending;

	bool backend_started = drm->base.started;
	bool connected = conn->connection == DRM_MODE_CONNECTED;
	bool enabled = user_pending->enabled && connected;
	bool active = state_curr->crtc.active;

	*need_start = backend_started && enabled && !active;
	*need_continue = backend_started && enabled && active;
	*need_stop = backend_started && !enabled && active;
	*need_remove = backend_started && !connected;

	UPDATE_PENDING(output, crtc.active, enabled, TW_DRM_PENDING_ACTIVE);

	/* bool connect_change, active_change; */
	/* bool pending_connect, pending_disconnect; */
	/* bool pending_activate, pending_deactivate; */

	/* connect_change = (output->status.pending & TW_DRM_PENDING_CONNECT); */
	/* active_change = (output->status.pending & TW_DRM_PENDING_ACTIVE); */

	/* pending_connect = (output->status.connected && connect_change); */
	/* pending_disconnect = (!output->status.connected && connect_change); */

	/* pending_activate = (output->status.active && active_change); */
	/* pending_deactivate = (!output->status.active && active_change); */

	/* //need_find_crtc happens after recollecting resources, all the display */
	/* //loses its crtc. */

	/* if (need_continue) */
	/*	*need_continue = (output->status.active && */
	/*	                  output->status.connected && */
	/*	                  !output->crtc && backend_started); */
	/* if (need_start) */
	/*	*need_start = (pending_connect || pending_activate) && */
	/*		backend_started; */
	/* if (need_stop) */
	/*	*need_stop = (pending_disconnect || pending_deactivate) && */
	/*		backend_started; */
}
