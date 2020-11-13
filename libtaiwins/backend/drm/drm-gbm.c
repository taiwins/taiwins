/*
 * drm-gbm.c - taiwins server drm-gbm functions
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
#include <gbm.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/drm_formats.h>
#include <taiwins/render_context.h>

#include "internal.h"

bool
tw_drm_init_gbm(struct tw_drm_backend *drm)
{
	drm->gbm.dev = gbm_create_device(drm->gpu_fd);
	if (!drm->gbm.dev) {
		tw_logl_level(TW_LOG_ERRO, "Failed to create gbm device");
		return false;
	}
	drm->gbm.visual_id = GBM_FORMAT_ARGB8888;

	return true;
}

static inline void
tw_drm_display_fini_gbm(struct tw_drm_display *output)
{
	if (output->gbm_surface.gbm) {
		tw_render_presentable_fini(&output->output.surface,
		                           output->drm->base.ctx);
		gbm_surface_destroy(output->gbm_surface.gbm);
		output->gbm_surface.gbm = NULL;
	}
}

/*
 * creating a buffer for for the output. We will allocate a buffer as same size
 * as the selected mode. In general, we allocate this buffer for a given plane.
 */
bool
tw_drm_display_start_gbm(struct tw_drm_display *output)
{
	struct tw_drm_backend *drm = output->drm;
	unsigned w = output->status.inherited_mode.w;
	unsigned h = output->status.inherited_mode.h;
	uint32_t scanout_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
	struct tw_drm_plane *plane = output->primary_plane;
	const struct tw_drm_format *format =
		tw_drm_format_find(&plane->formats, drm->gbm.visual_id);
	const struct tw_drm_modifier *mods =
		tw_drm_modifiers_get(&plane->formats, format);

	//ensure we have at least one mods
	uint64_t modifiers[format->len+1];
	for (int i = 0; i < format->len; i++)
		modifiers[i] = mods[i].modifier;

	tw_drm_display_fini_gbm(output);
	output->gbm_surface.gbm =
		tw_drm_create_gbm_surface(drm->gbm.dev, w, h,
		                          drm->gbm.visual_id,
		                          format->len, modifiers,
		                          scanout_flags);
	tw_render_presentable_init_window(&output->output.surface,
	                                  drm->base.ctx,
	                                  output->gbm_surface.gbm);
	return true;
}

struct gbm_surface *
tw_drm_create_gbm_surface(struct gbm_device *dev, uint32_t w, uint32_t h,
                          uint32_t format, int n_mods, uint64_t *modifiers,
                          uint32_t flags)
{
	struct gbm_surface *surf =
		gbm_surface_create_with_modifiers(dev, w, h, format,
		                                  modifiers, n_mods);
	if (!surf)
		surf = gbm_surface_create(dev, w, h, format, flags);

	return surf;
}
