/*
 * backend.c - taiwins server drm plane functions
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

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <drm_fourcc.h>
#include <wayland-util.h>
#include <xf86drmMode.h>
#include <taiwins/objects/drm_formats.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/plane.h>

#include "internal.h"

static inline uint32_t *
get_formats(struct drm_format_modifier_blob *blob)
{
	return (uint32_t *)(((char *)blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
get_modifiers(struct drm_format_modifier_blob *blob)
{
	return (struct drm_format_modifier *)
		(((char *)blob) + blob->modifiers_offset);
}

static inline bool
check_mod_has_fmt(struct drm_format_modifier *mod, unsigned idx)
{
	bool in_range = (idx >= mod->offset) && (idx < mod->offset+64);
	bool intersect = (mod->formats & (1 << (idx - mod->offset)));
	return in_range && intersect;
}

static void
populate_plane_formats(struct tw_drm_plane *plane, drmModePlane *drm_plane,
                       int fd)
{
	uint64_t blob_id;
	drmModePropertyBlobRes *blob;
	struct drm_format_modifier_blob *fm_blob;
	struct drm_format_modifier *blob_modifiers;
	uint32_t *blob_formats;

	if (!tw_drm_get_property(fd, drm_plane->plane_id,
	                         DRM_MODE_OBJECT_PLANE, "IN_FORMATS",
	                         &blob_id))
		goto fallback;

	blob = drmModeGetPropertyBlob(fd, blob_id);
	if (!blob)
		goto fallback;

	fm_blob = blob->data;
	blob_formats = get_formats(fm_blob);
	blob_modifiers = get_modifiers(fm_blob);
	if (drm_plane->count_formats != fm_blob->count_formats) {
		tw_logl_level(TW_LOG_ERRO, "format count different "
		              "between plane(%d) and IN_FORMATS (%d)",
		              drm_plane->count_formats,
		              fm_blob->count_formats);
		abort();
	}
	for (unsigned i = 0; i < fm_blob->count_formats; i++) {
		if (fm_blob->count_modifiers == 0)
			continue;
		uint64_t mods[fm_blob->count_modifiers];
		bool externals[fm_blob->count_modifiers];
		unsigned count_mods = 0;

		//we do not care about external_only flags
		memset(externals, 0, sizeof(bool) * fm_blob->count_modifiers);
		for (unsigned j = 0; j < fm_blob->count_modifiers; j++) {
			struct drm_format_modifier *mod = &blob_modifiers[j];
			if (!check_mod_has_fmt(mod, i))
				continue;
			mods[count_mods++] = mod->modifier;
		}
		tw_drm_formats_add_format(&plane->formats, blob_formats[i],
		                          count_mods, mods, externals);
	}
	return;
fallback:
	for (unsigned i = 0; i < drm_plane->count_formats; i++) {
		uint64_t invalid_mod = DRM_FORMAT_MOD_LINEAR;
		bool external = false;
		tw_drm_formats_add_format(&plane->formats,
		                          drm_plane->formats[i],
		                          1, &invalid_mod, &external);
	}
}

bool
tw_drm_plane_init(struct tw_drm_plane *plane, int fd, drmModePlane *drm_plane)
{
	uint64_t t;

	if (!tw_drm_get_property(fd, drm_plane->plane_id,
	                         DRM_MODE_OBJECT_PLANE, "type", &t))
		return false;

	if (t == DRM_PLANE_TYPE_CURSOR)
		plane->type = TW_DRM_PLANE_CURSOR;
	else if (t == DRM_PLANE_TYPE_OVERLAY)
		plane->type = TW_DRM_PLANE_OVERLAY;
	else
		plane->type = TW_DRM_PLANE_MAJOR;
	tw_plane_init(&plane->base);
	tw_drm_formats_init(&plane->formats);
	plane->id = drm_plane->plane_id;
	plane->crtc_mask = drm_plane->possible_crtcs;
	populate_plane_formats(plane, drm_plane, fd);
	return true;
}

void
tw_drm_plane_fini(struct tw_drm_plane *plane)
{
	tw_drm_formats_fini(&plane->formats);
	tw_plane_fini(&plane->base);
}
