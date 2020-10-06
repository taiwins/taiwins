/*
 * drm_formats.h - taiwins DRM format set interface
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

#ifndef TW_DRM_FORMATS_H
#define TW_DRM_FORMATS_H

#include <stdint.h>
#include <wayland-server.h>
#include <wayland-util.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_drm_format {
	uint32_t fmt;
	int cursor, len;
};

//maybe we just use this, making code much simpler
struct tw_drm_modifier {
	uint64_t modifier;
	bool external;
};

struct tw_drm_formats {
	struct wl_array formats;
	struct wl_array modifiers;
};

void
tw_drm_formats_init(struct tw_drm_formats *formats);

void
tw_drm_formats_fini(struct tw_drm_formats *formats);

size_t
tw_drm_formats_count(struct tw_drm_formats *formats);

bool
tw_drm_formats_add_format(struct tw_drm_formats *formats, uint32_t format,
                          int n_mods, uint64_t *modifiers,
                          bool *externals);
bool
tw_drm_formats_is_modifier_external(struct tw_drm_formats *formats,
                                    uint32_t format, uint64_t modifier);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
