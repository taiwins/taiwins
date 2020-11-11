/*
 * drm_formats.c - taiwins DRM format set implementation
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

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <wayland-util.h>

#include <taiwins/objects/drm_formats.h>

WL_EXPORT void
tw_drm_formats_init(struct tw_drm_formats *formats)
{
	wl_array_init(&formats->formats);
	wl_array_init(&formats->modifiers);
}

WL_EXPORT void
tw_drm_formats_fini(struct tw_drm_formats *formats)
{
	wl_array_release(&formats->formats);
	wl_array_release(&formats->modifiers);
}

WL_EXPORT size_t
tw_drm_formats_count(struct tw_drm_formats *formats)
{
	return formats->formats.size / sizeof(struct tw_drm_format);
}

WL_EXPORT bool
tw_drm_formats_add_format(struct tw_drm_formats *formats, uint32_t fmt,
                          int n_mods, uint64_t *modifiers, bool *externals)
{
	struct tw_drm_format *format = NULL;

	int cursor = formats->modifiers.size / sizeof(struct tw_drm_modifier);
	size_t size = n_mods * sizeof(struct tw_drm_modifier);
	struct tw_drm_modifier *mods =
		(n_mods) ? wl_array_add(&formats->modifiers, size) : NULL;

	if (!mods && (n_mods != 0))
		return false;
	//if this allocation failed, we would left with wasted space
	if (!(format = wl_array_add(&formats->formats,
	                            sizeof(struct tw_drm_format))))
		return false;

	//finally coping the data
	format->cursor = cursor;
	format->len = n_mods;
	format->fmt = fmt;
	for (int i = 0; i < n_mods; i++) {
		mods[i].modifier = modifiers[i];
		mods[i].external = externals[i];
	}
	return true;
}

WL_EXPORT bool
tw_drm_formats_is_modifier_external(struct tw_drm_formats *formats,
                                    uint32_t fmt, uint64_t mod)
{
	struct tw_drm_format *format;
	struct tw_drm_modifier *modifiers;
	bool external_only = false;

	wl_array_for_each(format, &formats->formats) {
		if (fmt != format->fmt)
			continue;
		modifiers = formats->modifiers.data;
		modifiers = modifiers + format->cursor;
		for (int i = 0; i < format->len; i++) {
			if (modifiers[i].modifier == mod)
				external_only = modifiers[i].external;
		}
	}
	return external_only;
}

WL_EXPORT const struct tw_drm_format *
tw_drm_format_find(const struct tw_drm_formats *formats, uint32_t fmt)
{
	const struct tw_drm_format *format;

	wl_array_for_each(format, &formats->formats) {
		if (format->fmt == fmt)
			return format;
	}
	return NULL;
}

WL_EXPORT const struct tw_drm_modifier *
tw_drm_modifiers_get(const struct tw_drm_formats *formats,
                     const struct tw_drm_format *fmt)
{
	const struct tw_drm_modifier *mods = formats->modifiers.data;

	mods += fmt->cursor;
	return (fmt->len == 0) ? NULL : mods;
}
