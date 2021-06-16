/*
 * output.c - taiwins server output implementation
 *
 * Copyright (c) 2021 Xichen Zhou
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
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/engine.h>
#include <taiwins/backend.h>
#include <taiwins/render_output.h>
#include <taiwins/render_context.h>
#include <taiwins/render_surface.h>
#include <ctypes/helpers.h>
#include <wayland-util.h>

#include "options.h"
#include "utils.h"
#include "output.h"

static void
tw_server_output_fini(struct tw_server_output *output);

static inline void
update_output_frame_time(struct tw_server_output *output,
                         const struct timespec *strt,
                         const struct timespec *end)
{
	uint32_t ft;
	uint64_t tstart = tw_timespec_to_us(strt);
	uint64_t tend = tw_timespec_to_us(end);

	/* assert(tend >= tstart); */
	ft = MAX((uint32_t)0, (uint32_t)(tend - tstart));
	output->state.fts[output->state.ft_idx] = ft;
	output->state.ft_idx = (output->state.ft_idx + 1) % TW_FRAME_TIME_CNT;
}

/* getting the max frame time in milliseconds */
static inline uint32_t
calc_output_max_frametime(struct tw_server_output *output)
{
	uint32_t *fts = output->state.fts;
	uint32_t ft = MAX(MAX(MAX(fts[0], fts[1]), MAX(fts[2],fts[3])),
	                  MAX(MAX(fts[4], fts[5]), MAX(fts[6],fts[7])));
	//ceil algorithm, output basically
	return ft ? ((ft + 1000) / 1000) : ft;
}

/******************************************************************************
 * surface output relations
 *****************************************************************************/

static void
update_surface_mask(struct tw_surface *base, struct tw_engine *engine,
                    struct tw_render_output *major, uint32_t mask)
{
	struct tw_engine_output *output;
	struct tw_render_surface *surface =
		wl_container_of(base, surface, surface);
	uint32_t output_bit;
	uint32_t different = surface->output_mask ^ mask;
	uint32_t entered = mask & different;
	uint32_t left = surface->output_mask & different;

	//update the surface_mask and
	surface->output_mask = mask;
	surface->output = major ? major->device.id : -1;

	wl_list_for_each(output, &engine->heads, link) {

		output_bit = 1u << output->device->id;
		if (!(output_bit & different))
			continue;
		if ((output_bit & entered))
			tw_engine_output_notify_surface_enter(output, base);
		if ((output_bit & left))
			tw_engine_output_notify_surface_leave(output, base);
	}
}

/* we employ the same logic for surface output assigning in weston, compares
 * the surface 2D geometry against the output
 */
static void
reassign_surface_outputs(struct tw_render_surface *render_surface,
                         struct tw_render_context *ctx,
                         struct tw_engine *engine)
{
	uint32_t area = 0, max = 0, mask = 0;
	struct tw_render_output *output, *major = NULL;
	pixman_region32_t surface_region;
	pixman_box32_t *e;
	struct tw_surface *surface = &render_surface->surface;

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

	update_surface_mask(surface, engine, major, mask);
}

/******************************************************************************
 * output listeners
 *****************************************************************************/

static int
notify_output_frame(void *data)
{
	struct tw_server_output *output = data;
	struct tw_render_output *render_output =
		wl_container_of(output->device, render_output, device);
	tw_render_output_post_frame(render_output);
	return 0;
}

static void
notify_output_reshedule_frame(struct wl_listener *listener, void *data)
{
	int delay, ms_left = 0; //< left for render
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.need_frame);
	struct tw_output_device *device = data;

	int frametime = calc_output_max_frametime(output);

	assert(output->device == device);
	if (!device->current.enabled)
		return;

	if (frametime) { //becomes max_render_time
		struct timespec now;
		//get current time as soon as possible
		clock_gettime(device->clk_id, &now);

		struct timespec predict_refresh = output->state.last_present;
		unsigned mhz = device->current.current_mode.refresh;
		uint32_t refresh = tw_millihertz_to_ns(mhz);

		//getting a predicted vblank. 1): If we are scheduled right
		//after a vsync, there are chance predict_refresh is ahead of
		//us. 2) If we come from a idle frame, predict_time will be
		//less than now, in that case, we just draw

		predict_refresh.tv_nsec += refresh % TW_NS_PER_S;
		predict_refresh.tv_sec += refresh / TW_NS_PER_S;
		if (predict_refresh.tv_nsec >= TW_NS_PER_S) {
			predict_refresh.tv_sec += 1;
			predict_refresh.tv_nsec -= TW_NS_PER_S;
		}

		//this is the floored difference.
		if (predict_refresh.tv_sec >= now.tv_sec)
			ms_left = tw_timespec_diff_ms(&predict_refresh, &now);
	}
	//here we added 2 extra ms frametime, it seems with amount, we are
	//able to catch up with next vblank. TODO we can we not rely on this,
	//otherwise we would have to move this repaint logic out of libtaiwins.
	delay = (ms_left - (frametime + 2));

	if (delay < 1) {
		notify_output_frame(output);
	} else {
		wl_event_source_timer_update(output->state.frame_timer, delay);
	}
}

static void
notify_output_pre_frame(struct wl_listener *listener, void *data)
{
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.pre_frame);
	struct tw_output_device *device = output->device;

	clock_gettime(device->clk_id, &output->state.ts);
	PROFILE_BEG("notify_output_repaint");
}

static void
notify_output_post_frame(struct wl_listener *listener, void *data)
{
	struct timespec now;
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.post_frame);
	struct tw_render_output *render_output =
		wl_container_of(output->device, render_output, device);

	clock_gettime(output->device->clk_id, &now);
	update_output_frame_time(output, &output->state.ts, &now);
	tw_render_output_flush_frame(render_output, &now);
	PROFILE_END("notify_output_repaint");
}

static void
notify_output_present(struct wl_listener *listener, void *data)
{
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.present);
	struct tw_event_output_present *event = data;

	output->state.last_present = event->time;
	SCOPE_PROFILE_TS();
}

static void
notify_output_clock_reset(struct wl_listener *listener, void *data)
{
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.clock_reset);
	output->state.ft_idx = 0;
	memset(output->state.fts, 0, sizeof(output->state.fts));
}

static void
notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_server_output *output =
		wl_container_of(listener, output, listeners.destroy);

        tw_server_output_fini(output);
	//this basically on output lost
}

/******************************************************************************
 * constructor/destructor
 *****************************************************************************/

static void
tw_server_output_fini(struct tw_server_output *output)
{
	wl_event_source_remove(output->state.frame_timer);
	output->state.frame_timer = NULL;
	output->device = NULL;

	tw_reset_wl_list(&output->listeners.destroy.link);
	tw_reset_wl_list(&output->listeners.need_frame.link);
	tw_reset_wl_list(&output->listeners.pre_frame.link);
	tw_reset_wl_list(&output->listeners.post_frame.link);
	tw_reset_wl_list(&output->listeners.present.link);
}

static void
tw_server_output_init(struct tw_server_output *output,
                      struct tw_output_device *device,
                      struct tw_render_context *ctx)
{
	struct tw_render_output *render_output =
		wl_container_of(device, render_output, device);
	struct wl_display *display = ctx->display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	output->device = device;
        output->state.frame_timer =
	        wl_event_loop_add_timer(loop, notify_output_frame, output);

        tw_signal_setup_listener(&render_output->signals.need_frame,
                                 &output->listeners.need_frame,
                                 notify_output_reshedule_frame);
        tw_signal_setup_listener(&render_output->signals.pre_frame,
                                 &output->listeners.pre_frame,
                                 notify_output_pre_frame);
        tw_signal_setup_listener(&render_output->signals.post_frame,
                                 &output->listeners.post_frame,
                                 notify_output_post_frame);
        tw_signal_setup_listener(&render_output->signals.present,
                                 &output->listeners.present,
                                 notify_output_present);
        //device signals
        tw_signal_setup_listener(&device->signals.clock_reset,
                                 &output->listeners.clock_reset,
                                 notify_output_clock_reset);
        tw_signal_setup_listener(&device->signals.destroy,
                                 &output->listeners.destroy,
                                 notify_output_destroy);
}

/******************************************************************************
 * manager listeners
 *****************************************************************************/

static void
notify_mgr_tw_surface_dirty(struct wl_listener *listener, void *data)
{
	struct tw_server_output_manager *mgr =
		wl_container_of(listener, mgr, listeners.surface_dirty);
	struct tw_surface *surface = data;
	struct tw_render_surface *render_surface =
		wl_container_of(surface, render_surface, surface);
	struct tw_render_context *ctx = mgr->ctx;
	struct tw_render_output *output;

	if (pixman_region32_not_empty(&surface->geometry.dirty))
		reassign_surface_outputs(render_surface, ctx, mgr->engine);

	wl_list_for_each(output, &ctx->outputs, link) {
		if ((1u << output->device.id) & render_surface->output_mask)
			tw_render_output_dirty(output);
	}
}

static void
notify_mgr_tw_surface_lost(struct wl_listener *listener, void *data)
{
	//TODO just dirty the output regards the surface damage.
}

static void
notify_mgr_new_output(struct wl_listener *listener, void *data)
{
	struct tw_server_output_manager *mgr =
		wl_container_of(listener, mgr, listeners.new_output);
	struct tw_output_device *device = data;
	unsigned id = device->id;

	tw_server_output_init(&mgr->outputs[id], device, mgr->ctx);
}

static void
notify_mgr_render_context_lost(struct wl_listener *listener, void *data)
{
	struct tw_server_output_manager *mgr =
		wl_container_of(listener, mgr, listeners.context_destroy);

        mgr->ctx = NULL;
	mgr->engine = NULL;

	tw_reset_wl_list(&mgr->listeners.context_destroy.link);
	tw_reset_wl_list(&mgr->listeners.surface_dirty.link);
	tw_reset_wl_list(&mgr->listeners.surface_lost.link);
	tw_reset_wl_list(&mgr->listeners.new_output.link);
}

struct tw_server_output_manager *
tw_server_output_manager_create_global(struct tw_engine *engine,
                                       struct tw_render_context *ctx)
{
	static struct tw_server_output_manager mgr = {0};
	struct tw_backend *backend = engine->backend;

	mgr.engine = engine;
	mgr.ctx = ctx;

	tw_signal_setup_listener(&backend->signals.new_output,
	                         &mgr.listeners.new_output,
	                         notify_mgr_new_output);
	tw_signal_setup_listener(&ctx->signals.wl_surface_dirty,
	                         &mgr.listeners.surface_dirty,
	                         notify_mgr_tw_surface_dirty);
	tw_signal_setup_listener(&ctx->signals.wl_surface_destroy,
	                         &mgr.listeners.surface_lost,
	                         notify_mgr_tw_surface_lost);
	tw_signal_setup_listener(&ctx->signals.destroy,
	                         &mgr.listeners.context_destroy,
	                         notify_mgr_render_context_lost);
	return &mgr;
}
