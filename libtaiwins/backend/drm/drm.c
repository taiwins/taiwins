/*
 * drm.c - taiwins server drm functions
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
#include <wayland-server.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/render_output.h>

#include "drm_mode.h"
#include "internal.h"

static inline void
tw_drm_crtc_init(struct tw_drm_crtc *crtc, int fd, int id, int idx)
{
	struct tw_drm_prop_info prop_info[] = {
		{"ACTIVE", &crtc->props.active},
		{"MODE_ID", &crtc->props.mode_id},
	};

	crtc->display = NULL;
	crtc->id = id;
	crtc->idx = idx;
	wl_list_init(&crtc->link);
	tw_drm_read_properties(fd, id, DRM_MODE_OBJECT_CRTC, prop_info,
	                       sizeof(prop_info) / sizeof(prop_info[0]));
}

static inline void
tw_drm_crtc_fini(struct tw_drm_crtc *crtc)
{
	crtc->display = NULL;
	crtc->id = -1;
	crtc->idx = -1;
	wl_list_remove(&crtc->link);
}

static void
add_display(struct tw_drm_gpu *gpu, drmModeConnector *conn)
{
	struct tw_drm_display *output = NULL;

	if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
		//TODO handle writeback connector
	} else {
		output = tw_drm_display_find_create(gpu, conn);
		if (output) {
			bool need_start, need_stop, need_continue;

			if (!tw_drm_display_read_info(output, conn))
				tw_logl_level(TW_LOG_WARN, "failed to read "
				              "current mode from output");
			tw_drm_display_check_start_stop(output, &need_start,
			                                &need_stop,
			                                &need_continue);
			if (need_start)
				tw_drm_display_start(output);
			else if (need_continue)
				tw_drm_display_continue(output);
			else if (need_stop)
				tw_drm_display_stop(output);
		}
	}
}

static void
collect_connectors(struct tw_drm_gpu *gpu, drmModeRes *res)
{
	int fd = gpu->gpu_fd;
	//We may now skip the connected not connected
	if (!gpu->boot_vga)
		return;

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn;
		uint32_t conn_id = res->connectors[i];

		conn = drmModeGetConnector(fd, conn_id);
		if (!conn)
			continue;
		add_display(gpu, conn);

		//add connectors
		drmModeFreeConnector(conn);
	}
}

static void
collect_crtcs(struct tw_drm_gpu *gpu, drmModeRes *res)
{
	int fd = gpu->gpu_fd;
	gpu->crtc_mask = 0;
	//TODO, MAX 32
	wl_list_init(&gpu->crtc_list);
	for (int i = 0; i < res->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc)
			continue;
		tw_drm_crtc_init(&gpu->crtcs[i], fd, crtc->crtc_id, i);
		wl_list_insert(gpu->crtc_list.prev, &gpu->crtcs[i].link);
		gpu->crtc_mask |= 1 << i;

		drmModeFreeCrtc(crtc);
	}
}

static void
collect_planes(struct tw_drm_gpu *gpu)
{
	int fd = gpu->gpu_fd;
	struct tw_drm_plane *p;
	drmModePlane *plane;
	drmModePlaneRes *planes = drmModeGetPlaneResources(fd);

        gpu->plane_mask = 0;
        if (!planes)
		return;
        //it seems we have 3 planes for every crtc.
        wl_list_init(&gpu->plane_list);
        for (unsigned i = 0; i < planes->count_planes; i++) {
	        p = &gpu->planes[i];
	        plane = drmModeGetPlane(fd, planes->planes[i]);

	        if (!plane)
		        continue;
	        if (!tw_drm_plane_init(p, fd, plane))
		        continue;

	        gpu->plane_mask |= 1 << i;
	        wl_list_insert(gpu->plane_list.prev, &p->base.link);
	        drmModeFreePlane(plane);
        }

        drmModeFreePlaneResources(planes);
}

bool
tw_drm_check_gpu_resources(struct tw_drm_gpu *gpu)
{
	int fd = gpu->gpu_fd;
	drmModeRes *resources;

	resources = drmModeGetResources(fd);
	if (!resources) {
		tw_logl_level(TW_LOG_ERRO, "drmModeGetResources failed");
		return false;
	}
	collect_crtcs(gpu, resources);
	collect_planes(gpu);
	collect_connectors(gpu, resources);

	gpu->limits.min_width = resources->min_width;
	gpu->limits.max_width = resources->max_width;
	gpu->limits.min_height = resources->min_height;
	gpu->limits.max_height = resources->max_height;

	drmModeFreeResources(resources);

	return true;
}

void
tw_drm_print_info(int fd)
{
	char *name = drmGetDeviceNameFromFd2(fd);
	drmVersion *version = drmGetVersion(fd);

	tw_logl("Initializing DRM backend %s (%s)", name, version);

	free(name);
	drmFreeVersion(version);
}

static void
handle_page_flip2(int fd, unsigned seq, unsigned tv_sec, unsigned tv_usec,
                  unsigned crtc_id, void *data)
{
	struct tw_drm_display *output = data;
	struct tw_drm_gpu *gpu = output ? output->gpu : NULL;

	if (output) {
		assert(gpu->gpu_fd == fd);
		if (output->status.crtc_id == TW_DRM_CRTC_ID_INVALID) {
			return;
		} else {
			assert(output->status.crtc_id == (int)crtc_id);
			gpu->impl->page_flip(output, DRM_MODE_PAGE_FLIP_EVENT);
		}
	}
	//TODO: send present event
}

int
tw_drm_handle_drm_event(int fd, uint32_t mask, void *data)
{
	drmEventContext event = {
		.version = 3,
		.vblank_handler = NULL,
		.page_flip_handler2 = handle_page_flip2,
		.sequence_handler = NULL,
	};
	struct tw_drm_gpu *gpu = data;

	if (gpu->activated)
		drmHandleEvent(fd, &event);
        return 1;
}

bool
tw_drm_check_gpu_features(struct tw_drm_gpu *gpu)
{
	uint64_t cap;
	int fd = gpu->gpu_fd;

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		tw_logl_level(TW_LOG_ERRO, "DRM planes not supported");
		return false;
	}
	if (drmGetCap(fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) || !cap) {
		tw_logl_level(TW_LOG_ERRO, "DRM vblank event not supported");
		return false;
	}

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0) {
		gpu->feats |= TW_DRM_CAP_ATOMIC;
	}

	if (drmGetCap(fd, DRM_CAP_PRIME, &cap) ||
	    !(cap & DRM_PRIME_CAP_EXPORT) || !(cap & DRM_PRIME_CAP_IMPORT)) {
		tw_logl_level(TW_LOG_ERRO, "PRIME support not complete");
		return false;
	} else {
		gpu->feats |= TW_DRM_CAP_PRIME;
	}

	if (drmGetCap(fd, DRM_CAP_ADDFB2_MODIFIERS, &cap) == 0) {
		if (cap == 1)
			gpu->feats |= TW_DRM_CAP_MODIFIERS;
	}

        if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &cap) == 0) {
		if (cap == 1)
			gpu->feats |= TW_DRM_CAP_DUMPBUFFER;
	}

	return true;
}

void
tw_drm_free_gpu_resources(struct tw_drm_gpu *gpu)
{
	struct tw_drm_plane *p, *ptmp;
	struct tw_drm_display *d, *dtmp;
	struct tw_drm_crtc *c, *ctmp;
	struct tw_backend *backend = &gpu->drm->base;

	wl_list_for_each_safe(p, ptmp, &gpu->plane_list, base.link)
		tw_drm_plane_fini(p);
	wl_list_for_each_safe(d, dtmp, &backend->outputs, output.device.link)
		if (d->gpu == gpu)
			tw_drm_display_remove(d);
	wl_list_for_each_safe(c, ctmp, &gpu->crtc_list, link)
		tw_drm_crtc_fini(c);

	gpu->activated = false;
}

void
tw_drm_handle_gpu_event(struct tw_drm_gpu *gpu,
                        enum tw_drm_device_action action)
{
	switch (action) {
	case TW_DRM_DEV_UNKNOWN:
	case TW_DRM_DEV_ADD:
		//No need to handle these two
		break;
	case TW_DRM_DEV_RM:
		//for removing, we would need to close the resources
		tw_drm_backend_remove_gpu(gpu);
		break;
	case TW_DRM_DEV_OFFLINE:
		//set inactive, we don't process gpu_fd events no more.
		//more.
		wl_event_source_fd_update(gpu->event, 0);
		gpu->activated = false;
		break;
	case TW_DRM_DEV_ONLINE:
	case TW_DRM_DEV_CHANGE:
		wl_event_source_fd_update(gpu->event, WL_EVENT_READABLE);
		gpu->activated = true;
		tw_drm_check_gpu_resources(gpu);
		break;
	}
}

bool
tw_drm_get_property(int fd, uint32_t obj_id, uint32_t obj_type,
                    const char *prop_name, uint64_t *value)
{
	bool found = false;
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj_id,
	                                                            obj_type);
	if (!props)
		return false;
	for (unsigned i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop =
			drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;
		if (prop->prop_id != props->props[i])
			continue;
		if (strcmp(prop->name, prop_name) == 0) {
			*value = props->prop_values[i];
			found = true;
		}
		drmModeFreeProperty(prop);
		if (found)
			break;
	}
	drmModeFreeObjectProperties(props);
	return found;
}

static int
cmp_prop_info(const void *arg1, const void *arg2)
{
	const char *key = arg1;
	const struct tw_drm_prop_info *info = arg2;
	return strcmp(key, info->name);
}

bool
tw_drm_read_properties(int fd, uint32_t obj_id, uint32_t obj_type,
                       struct tw_drm_prop_info *info, size_t len)
{
	drmModeObjectProperties *props =
		drmModeObjectGetProperties(fd, obj_id, obj_type);

        for (unsigned i = 0; i < len; i++)
		*info[i].ptr = 0;

	if (!props)
		return false;

	for (unsigned i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop =
			drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;
		struct tw_drm_prop_info *i =
			bsearch(prop->name, info, len, sizeof(info[0]),
			        cmp_prop_info);
		if (i)
			*(i->ptr) = prop->prop_id;
		drmModeFreeProperty(prop);
	}

	return true;
}


enum tw_drm_device_action
tw_drm_device_action_from_name(const char *name)
{
	if (!name)
		return TW_DRM_DEV_UNKNOWN;
	else if (strcmp(name, "add") == 0)
		return TW_DRM_DEV_ADD;
	else if (strcmp(name, "remove") == 0)
		return TW_DRM_DEV_RM;
	else if (strcmp(name, "change") == 0)
		return TW_DRM_DEV_CHANGE;
	else if (strcmp(name, "online") == 0)
		return TW_DRM_DEV_ONLINE;
	else if (strcmp(name, "offline") == 0)
		return TW_DRM_DEV_OFFLINE;
	else
		return TW_DRM_DEV_UNKNOWN;
}
