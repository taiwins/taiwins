/*
 * render_output.c - taiwins render output
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

#include <GLES2/gl2.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include <pixman.h>
#include <wayland-util.h>

#include "render_context.h"
#include "render_output.h"
#include "output_device.h"
#include "render_pipeline.h"

static enum wl_output_transform
inverse_wl_transform(enum wl_output_transform t)
{
	if ((t & WL_OUTPUT_TRANSFORM_90) &&
	    !(t & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		t ^= WL_OUTPUT_TRANSFORM_180;
	}
	return t;
}

static void
init_output_state(struct tw_render_output *o)
{
	wl_list_init(&o->link);
	//okay, here is what we will need to fix
	for (int i = 0; i < 3; i++)
		pixman_region32_init(&o->state.damages[i]);

	o->state.dirty = true;
	o->state.repaint_state = TW_REPAINT_DIRTY;
	o->state.pending_damage = &o->state.damages[0];
	o->state.curr_damage = &o->state.damages[1];
	o->state.prev_damage = &o->state.damages[2];
	o->state.repaint_state = TW_REPAINT_DIRTY;
	tw_mat3_init(&o->state.view_2d);

}

static void
fini_output_state(struct tw_render_output *o)
{
	o->state.dirty = false;
	wl_list_remove(&o->link);

	for (int i = 0; i < 3; i++)
		pixman_region32_fini(&o->state.damages[i]);
}

static void
update_surface_mask(struct tw_surface *surface,
                    struct tw_render_output *major, uint32_t mask)
{
	struct tw_render_output *output;
	struct wl_resource *res;
	uint32_t output_bit;
	uint32_t different = surface->output_mask ^ mask;
	uint32_t entered = mask & different;
	uint32_t left = surface->output_mask & different;
	struct wl_client *client = wl_resource_get_client(surface->resource);
	(void)entered;
	(void)left;
	(void)client;
	(void)res;

	assert(major->ctx);

	//update the surface_mask and
	surface->output_mask = mask;
	surface->output = major->device.id;

	wl_list_for_each(output, &major->ctx->outputs, link) {
		output_bit = 1u << output->device.id;
		if (!(output_bit & different))
			continue;
		//TODO:
		// wl_resource_for_each(res, &output->wlr_output->resources) {
		//	if (client != wl_resource_get_client(res))
		//		continue;
		//	if ((output_bit & entered))
		//		wl_surface_send_enter(surface->resource, res);
		//	if ((output_bit & left))
		//		wl_surface_send_leave(surface->resource, res);
		// }
	}
}


static void
reassign_surface_outputs(struct tw_surface *surface,
                         struct tw_render_context *ctx)
{
	uint32_t area = 0, max = 0, mask = 0;
	struct tw_render_output *output, *major = NULL;
	pixman_region32_t surface_region;
	pixman_box32_t *e;

	pixman_region32_init_rect(&surface_region,
	                          surface->geometry.xywh.x,
	                          surface->geometry.xywh.y,
	                          surface->geometry.xywh.width,
	                          surface->geometry.xywh.height);
	wl_list_for_each(output, &ctx->outputs, link) {
		pixman_region32_t clip;
		struct tw_output_device *device = &output->device;
		pixman_rectangle32_t rect =
			tw_output_device_geometry(device);
		//TODO dealing with cloning output
		// if (output->cloning >= 0)
		//	continue;
		pixman_region32_init_rect(&clip, rect.x, rect.y,
		                          rect.width, rect.height);
		pixman_region32_intersect(&clip, &clip, &surface_region);
		e = pixman_region32_extents(&clip);
		area = (e->x2 - e->x1) * (e->y2 - e->y1);
		if (pixman_region32_not_empty(&clip))
			mask |= (1u << device->id);
		if (area >= max) {
			major = output;
			max = area;
		}
		pixman_region32_fini(&clip);
	}
	pixman_region32_fini(&surface_region);

	update_surface_mask(surface, major, mask);
}

/**
 * @brief manage the backend output damage state
 */
static void
shuffle_output_damage(struct tw_render_output *output)
{
	//here we swap the damage as if it is output is triple-buffered. It is
	//okay even if output is actually double buffered, as we only need to
	//ensure that renderer requested the correct damage based on the age.
	pixman_region32_t *curr = output->state.curr_damage;
	pixman_region32_t *pending = output->state.pending_damage;
	pixman_region32_t *previous = output->state.prev_damage;

	//later on renderer will access either current or previous damage for
	//composing buffer_damage.
	output->state.curr_damage = pending;
	output->state.prev_damage = curr;
	output->state.pending_damage = previous;
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_output_surface_dirty(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.surface_dirty);
	struct tw_surface *surface = data;
	struct tw_render_context *ctx = output->ctx;

        assert(ctx);
	if (pixman_region32_not_empty(&surface->geometry.dirty))
		reassign_surface_outputs(surface, ctx);

	wl_list_for_each(output, &ctx->outputs, link) {
		if ((1u << output->device.id) & surface->output_mask)
			tw_render_output_dirty(output);
	}
}

static void
notify_output_frame(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.frame);
	struct tw_render_presentable *presentable = &output->surface;
	struct tw_render_context *ctx = output->ctx;
	struct tw_render_pipeline *pipeline;
	int buffer_age;

	assert(ctx);

	if (output->state.repaint_state != TW_REPAINT_DIRTY)
		return;

	buffer_age = tw_render_presentable_make_current(presentable, ctx);
	buffer_age = buffer_age > 2 ? 2 : buffer_age;

	wl_list_for_each(pipeline, &ctx->pipelines, link)
		tw_render_pipeline_repaint(pipeline, output, buffer_age);

	shuffle_output_damage(output);

        tw_render_presentable_commit(presentable, ctx);
        //presenting should happen here I guess
	tw_output_device_present(&output->device);

	//clean off the repaint state
	output->state.repaint_state = TW_REPAINT_CLEAN;
}

static void
notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.destroy);
	fini_output_state(output);
	wl_list_remove(&output->listeners.destroy.link);
	wl_list_remove(&output->listeners.frame.link);
	wl_list_remove(&output->listeners.set_mode.link);
	wl_list_remove(&output->listeners.surface_dirty.link);

	tw_render_presentable_fini(&output->surface, output->ctx);
}

static void
notify_output_new_mode(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.set_mode);
	tw_render_output_rebuild_view_mat(output);
}


/******************************************************************************
 * APIs
 *****************************************************************************/

void
tw_render_output_init(struct tw_render_output *output,
                      const struct tw_output_device_impl *impl)
{
	init_output_state(output);
	tw_output_device_init(&output->device, impl);

	wl_list_init(&output->link);
	wl_list_init(&output->listeners.surface_dirty.link);

	tw_signal_setup_listener(&output->device.events.new_frame,
	                         &output->listeners.frame,
	                         notify_output_frame);
	tw_signal_setup_listener(&output->device.events.destroy,
	                         &output->listeners.destroy,
	                         notify_output_destroy);
	tw_signal_setup_listener(&output->device.events.commit_state,
	                         &output->listeners.set_mode,
	                         notify_output_new_mode);
}

void
tw_render_output_fini(struct tw_render_output *output)
{
	assert(output->ctx);

	fini_output_state(output);
	tw_render_presentable_fini(&output->surface, output->ctx);
	tw_output_device_fini(&output->device);
}

void
tw_render_output_rebuild_view_mat(struct tw_render_output *output)
{
	struct tw_mat3 glproj, tmp;
	int width, height;
	const struct tw_output_device_state *state = &output->device.state;
	pixman_rectangle32_t rect = tw_output_device_geometry(&output->device);

	//the transform should be
	// T' = glproj * inv_wl_transform * scale * -translate * T
	width = rect.width;
	height = rect.height;

	//output scale and inverse transform.
	tw_mat3_translate(&output->state.view_2d, -state->gx, -state->gy);
	tw_mat3_transform_rect(&tmp, false,
	                       inverse_wl_transform(state->transform),
	                       width, height, state->scale);
	//glproj matrix,
	tw_mat3_init(&glproj);
	glproj.d[4] = -1;
	glproj.d[7] = state->current_mode.h;

	tw_mat3_multiply(&output->state.view_2d, &tmp,
	                 &output->state.view_2d);
	tw_mat3_multiply(&output->state.view_2d, &glproj,
	                 &output->state.view_2d);

}

void
tw_render_output_set_context(struct tw_render_output *output,
                             struct tw_render_context *ctx)
{
	assert(ctx);
	assert(!output->ctx);
	//insert into ctx
	output->ctx = ctx;
	tw_reset_wl_list(&output->link);
	wl_list_insert(ctx->outputs.prev, &output->link);

	tw_reset_wl_list(&output->listeners.surface_dirty.link);
	tw_signal_setup_listener(&ctx->events.wl_surface_dirty,
	                         &output->listeners.surface_dirty,
	                         notify_output_surface_dirty);
}

void
tw_render_output_dirty(struct tw_render_output *output)
{
	if (output->state.repaint_state != TW_REPAINT_CLEAN)
		return;
	output->state.repaint_state = TW_REPAINT_DIRTY;
	//TODO schedule repaint
}
