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

#include <assert.h>
#include <time.h>
#include <pixman.h>
#include <stdint.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/render_surface.h>
#include <taiwins/render_pipeline.h>
#include <ctypes/helpers.h>

static inline bool
check_bits(uint32_t data, uint32_t mask)
{
	return ((data ^ mask) & mask) == 0;
}

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

	o->state.pending_damage = &o->state.damages[0];
	o->state.curr_damage = &o->state.damages[1];
	o->state.prev_damage = &o->state.damages[2];
	o->state.repaint_state = TW_REPAINT_DIRTY;
	tw_mat3_init(&o->state.view_2d);
}

static void
fini_output_state(struct tw_render_output *o)
{
	wl_list_remove(&o->link);

	for (int i = 0; i < 3; i++)
		pixman_region32_fini(&o->state.damages[i]);
}

static void
rebuild_render_output_view_mat(struct tw_render_output *output)
{
	struct tw_mat3 glproj, tmp;
	int width, height;
	const struct tw_output_device_state *state = &output->device.current;
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

/**
 * @brief manage the backend output damage state
 */
static inline void
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

/*
 * after commit, the output should not dirty anymore, but the schedule state
 * should not change, it shield us from committing another frame before the
 * pageflip/swapbuffer happens.
*/
static inline void
commit_render_output(struct tw_render_output *output)
{
	output->state.repaint_state = TW_REPAINT_COMMITTED;
	tw_render_presentable_commit(&output->surface, output->ctx);
}

static int
tick_render_output_frame(struct tw_render_output *output)
{
	struct tw_render_presentable *presentable = &output->surface;
	struct tw_render_context *ctx = output->ctx;
	struct tw_render_pipeline *pipeline;
	int buffer_age;

	assert(ctx);
	buffer_age = tw_render_presentable_make_current(presentable, ctx);
	buffer_age = (buffer_age < 0) ? 2 : buffer_age;

	wl_list_for_each(pipeline, &ctx->pipelines, link)
		tw_render_pipeline_repaint(pipeline, output, buffer_age);

	shuffle_output_damage(output);
	commit_render_output(output);
	return 0;
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_render_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.destroy);
	tw_render_output_fini(output);
}

static void
notify_render_output_new_mode(struct wl_listener *listener, void *data)
{
	struct tw_render_output *output =
		wl_container_of(listener, output, listeners.set_mode);
	rebuild_render_output_view_mat(output);
}

/******************************************************************************
 * APIs
 *****************************************************************************/

WL_EXPORT void
tw_render_output_init(struct tw_render_output *output,
                      const struct tw_output_device_impl *impl,
                      struct wl_display *display)
{
	output->ctx = NULL;
	output->surface.impl = NULL;
	output->surface.handle = 0;
	init_output_state(output);

	tw_output_device_init(&output->device, impl);
	tw_output_device_reset_clock(&output->device, CLOCK_MONOTONIC);

	wl_list_init(&output->link);

	wl_signal_init(&output->surface.commit);
	wl_signal_init(&output->signals.need_frame);
	wl_signal_init(&output->signals.pre_frame);
	wl_signal_init(&output->signals.post_frame);
	wl_signal_init(&output->signals.surface_enter);
	wl_signal_init(&output->signals.surface_leave);

	tw_signal_setup_listener(&output->device.signals.destroy,
	                         &output->listeners.destroy,
	                         notify_render_output_destroy);
	tw_signal_setup_listener(&output->device.signals.commit_state,
	                         &output->listeners.set_mode,
	                         notify_render_output_new_mode);
}

WL_EXPORT void
tw_render_output_fini(struct tw_render_output *output)
{
	fini_output_state(output);
	wl_list_remove(&output->listeners.destroy.link);
	wl_list_remove(&output->listeners.set_mode.link);

	if (output->ctx && output->surface.impl)
		tw_render_presentable_fini(&output->surface, output->ctx);
	if (output->repaint_timer) {
		wl_event_source_remove(output->repaint_timer);
		output->repaint_timer = NULL;
	}
	tw_output_device_fini(&output->device);
}

WL_EXPORT void
tw_render_output_set_context(struct tw_render_output *output,
                             struct tw_render_context *ctx)
{
	assert(ctx);
	assert(!output->ctx);
	//insert into ctx
	output->ctx = ctx;
	tw_reset_wl_list(&output->link);
	wl_list_insert(ctx->outputs.prev, &output->link);
}

WL_EXPORT void
tw_render_output_unset_context(struct tw_render_output *output)
{
	struct tw_render_context *ctx = output->ctx;
	//should be safe to call multiple times
	assert(!output->surface.handle);
	output->ctx = NULL;
	tw_reset_wl_list(&output->link);
	wl_signal_emit(&ctx->signals.output_lost, output);
}

WL_EXPORT void
tw_render_output_flush_frame(struct tw_render_output *output,
                             const struct timespec *now)
{
	struct tw_surface *surface;
	uint32_t now_int = tw_timespec_to_ms(now);

	wl_list_for_each(surface, &output->views, links[TW_VIEW_OUTPUT_LINK])
		tw_surface_flush_frame(surface, now_int);
}

WL_EXPORT void
tw_render_output_dirty(struct tw_render_output *output)
{
	output->state.repaint_state |= TW_REPAINT_DIRTY;
	if (!(output->state.repaint_state & TW_REPAINT_SCHEDULED) &&
	    output->device.current.enabled)
		wl_signal_emit(&output->signals.need_frame, output);
}

WL_EXPORT void
tw_render_output_post_frame(struct tw_render_output *output)
{
	if (!output->device.current.enabled)
		return;
	if (!(output->state.repaint_state & TW_REPAINT_DIRTY))
		return;
	if ((output->state.repaint_state & TW_REPAINT_SCHEDULED))
		return;
	if (check_bits(output->state.repaint_state, TW_REPAINT_COMMITTED))
		return;

	output->state.repaint_state |= TW_REPAINT_SCHEDULED;
	wl_signal_emit(&output->signals.pre_frame, output);
	tick_render_output_frame(output);
	wl_signal_emit(&output->signals.post_frame, output);
}

/*
 * backends ought call this on swapbuffer/pageflip, it checks if the output is
 * still dirty and reset the TW_REPAINT_SCHEDULED bit so we can commit another
 * frame frame again.
 */
WL_EXPORT void
tw_render_output_clean_maybe(struct tw_render_output *output)
{
	output->state.repaint_state &= ~TW_REPAINT_COMMITTED;
	if (output->state.repaint_state & TW_REPAINT_DIRTY)
		tw_render_output_dirty(output);
}
