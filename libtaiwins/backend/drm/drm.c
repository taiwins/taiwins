/*
 * backend.c - taiwins server drm functions
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <wayland-util.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/render_output.h>

#include "internal.h"

//TODO: deal with hotplug?
static bool
tw_drm_add_output(struct tw_drm_backend *drm, drmModeConnector *conn)
{
	if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
		//TODO handle writeback connector
	} else {
		struct tw_drm_display *output = NULL;

		output = tw_drm_display_find_create(drm, conn);
		if (output) {

		}
	}
	return true;
}

static void
collect_connectors(struct tw_drm_backend *drm, int fd, drmModeRes *res)
{
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn;
		uint32_t conn_id = res->connectors[i];

		conn = drmModeGetConnector(fd, conn_id);
		if (!conn)
			continue;
		tw_drm_add_output(drm, conn);

		//add connectors
		drmModeFreeConnector(conn);
	}
}

static void
collect_crtcs(struct tw_drm_backend *drm, int fd, drmModeRes *res)
{
	drm->crtc_mask = 0;
	//TODO, MAX 32
	wl_list_init(&drm->crtc_list);
	for (int i = 0; i < res->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc)
			continue;

		drm->crtcs[i].id = crtc->crtc_id;
		drm->crtcs[i].idx = i;
		wl_list_init(&drm->crtcs[i].link);
		wl_list_insert(drm->crtc_list.prev, &drm->crtcs[i].link);
		drm->crtc_mask |= 1 << i;

		drmModeFreeCrtc(crtc);
	}
}

static void
collect_planes(struct tw_drm_backend *drm, int fd)
{
	struct tw_drm_plane *p;
	drmModePlane *plane;
	drmModePlaneRes *planes = drmModeGetPlaneResources(fd);

        if (!planes)
		return;

        //it seems we have 3 planes for every crtc.
        wl_list_init(&drm->plane_list);
        for (unsigned i = 0; i < planes->count_planes; i++) {
	        p = &drm->planes[i];
	        plane = drmModeGetPlane(fd, planes->planes[i]);

	        if (!plane)
		        continue;
	        if (!tw_drm_plane_init(p, fd, plane))
		        continue;

	        drm->plane_mask |= 1 << i;
	        wl_list_insert(drm->plane_list.prev, &p->base.link);
	        drmModeFreePlane(plane);
        }

        drmModeFreePlaneResources(planes);
}

bool
tw_drm_check_resources(struct tw_drm_backend *drm)
{
	int fd = drm->gpu_fd;
	drmModeRes *resources;

	resources = drmModeGetResources(fd);
	if (!resources) {
		tw_logl_level(TW_LOG_ERRO, "drmModeGetResources failed");
		return false;
	}
	collect_crtcs(drm, fd, resources);
	collect_connectors(drm, fd, resources);
	collect_planes(drm, fd);

	drm->limits.min_width = resources->min_width;
	drm->limits.max_width = resources->max_width;
	drm->limits.min_height = resources->min_height;
	drm->limits.max_height = resources->max_height;

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

int
tw_drm_handle_drm_event(int fd, uint32_t mask, void *data)
{
	drmEventContext event = {
		.version = 3,
		.vblank_handler = NULL,
		.page_flip_handler2 = NULL,
		.sequence_handler = NULL,
	};

	drmHandleEvent(fd, &event);
        return 1;
}

bool
tw_drm_check_features(struct tw_drm_backend *drm)
{
	uint64_t cap;
	int fd = drm->gpu_fd;

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		tw_logl_level(TW_LOG_ERRO, "DRM planes not supported");
		return false;
	}
	if (drmGetCap(fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) || !cap) {
		tw_logl_level(TW_LOG_ERRO, "DRM vblank event not supported");
		return false;
	}

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0) {
		drm->feats |= TW_DRM_CAP_ATOMIC;
		//TODO setup interface, maybe we do not
	}

	if (drmGetCap(drm->gpu_fd, DRM_CAP_PRIME, &cap) ||
	    !(cap & DRM_PRIME_CAP_EXPORT) || !(cap & DRM_PRIME_CAP_IMPORT)) {
		tw_logl_level(TW_LOG_ERRO, "PRIME support not complete");
		return false;
	} else {
		drm->feats |= TW_DRM_CAP_PRIME;
	}

	if (drmGetCap(drm->gpu_fd, DRM_CAP_ADDFB2_MODIFIERS, &cap) == 0) {
		if (cap == 1)
			drm->feats |= TW_DRM_CAP_MODIFIERS;
	}

        if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &cap) == 0) {
		if (cap == 1)
			drm->feats |= TW_DRM_CAP_DUMPBUFFER;
	}

	return true;
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

bool
tw_drm_set_property(int fd, uint32_t obj_id, uint32_t objtype,
                    const char *prop_name, uint64_t value)
{
	return false;
}
