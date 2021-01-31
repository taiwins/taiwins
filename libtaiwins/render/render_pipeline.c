/*
 * render_pipeline.c - taiwins render pipeline
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
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <taiwins/render_pipeline.h>

WL_EXPORT void
tw_render_pipeline_init(struct tw_render_pipeline *pipeline,
                        const char *name, struct tw_render_context *ctx)
{
	assert(ctx);
	pipeline->name = name;
	pipeline->ctx = ctx;
	wl_list_init(&pipeline->link);
	wl_list_init(&pipeline->ctx_destroy.link);

	wl_signal_init(&pipeline->signals.pre_output_repaint);
	wl_signal_init(&pipeline->signals.post_output_repaint);

}

WL_EXPORT void
tw_render_pipeline_fini(struct tw_render_pipeline *pipeline)
{
	wl_list_remove(&pipeline->link);
	wl_list_remove(&pipeline->ctx_destroy.link);
}
