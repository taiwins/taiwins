/*
 * output.c - taiwins engine output implementation
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
#include <stdlib.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/output.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>

#include "backend/backend.h"
#include "engine.h"
#include "internal.h"
#include "output_device.h"

static void
init_output_state(struct tw_engine_output *o)
{
	pixman_rectangle32_t rect = tw_output_device_geometry(o->device);
	o->state.dirty = true;
	//okay, here is what we will need to fix
	wl_list_init(&o->constrain.link);
	pixman_region32_init_rect(&o->constrain.region,
	                          rect.x, rect.y, rect.width, rect.height);

	wl_list_insert(o->engine->global_cursor.constrains.prev,
	               &o->constrain.link);
	for (int i = 0; i < 3; i++)
		pixman_region32_init(&o->state.damages[i]);

	o->state.repaint_state = TW_REPAINT_DIRTY;
	o->state.pending_damage = &o->state.damages[0];
	o->state.curr_damage = &o->state.damages[1];
	o->state.prev_damage = &o->state.damages[2];
}

static void
fini_output_state(struct tw_engine_output *o)
{
	o->state.dirty = false;
	tw_reset_wl_list(&o->constrain.link);
	pixman_region32_fini(&o->constrain.region);
	for (int i = 0; i < 3; i++)
		pixman_region32_fini(&o->state.damages[i]);
}

static void
notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.destroy);
	struct tw_engine *engine = output->engine;
	uint32_t unset = ~(1 << output->id);

	output->id = -1;
	wl_list_remove(&output->link);
	wl_list_remove(&output->listeners.destroy.link);
	wl_list_remove(&output->listeners.frame.link);
	wl_list_remove(&output->listeners.info.link);

	//TODO we should have this
	//tw_output_destroy(output->tw_output);

	fini_output_state(output);
	engine->output_pool &= unset;
	wl_signal_emit(&engine->events.output_remove, output);
}

static void
notify_output_frame(struct wl_listener *listener, void *data)
{
	//TODO
}

static void
notify_output_info(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.info);
}

static void
notify_output_new_mode(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.set_mode);
	pixman_rectangle32_t rect = tw_output_device_geometry(output->device);

	pixman_region32_fini(&output->constrain.region);
	pixman_region32_init_rect(&output->constrain.region,
	                          rect.x, rect.y, rect.width, rect.width);
}

/******************************************************************************
 * internal APIs
 *****************************************************************************/
struct tw_engine_output *
tw_engine_pick_output_for_cursor(struct tw_engine *engine)
{
	pixman_region32_t *output_region;
	struct tw_engine_output *output;

	wl_list_for_each(output, &engine->heads, link) {
		if (output->cloning >= 0)
			continue;
		output_region = &output->constrain.region;
		if (pixman_region32_contains_point(output_region,
		                                   engine->global_cursor.x,
		                                   engine->global_cursor.y,
		                                   NULL))
			return output;
	}
	return NULL;
}


/******************************************************************************
 * APIs
 *****************************************************************************/

bool
tw_engine_new_output(struct tw_engine *engine,
                     struct tw_output_device *device)
{
	struct tw_engine_output *output;
	uint32_t id = ffs(~engine->output_pool)-1;

	if (ffs(!engine->output_pool) <= 0)
		tw_logl_level(TW_LOG_ERRO, "too many displays");
	output = &engine->outputs[id];
	output->id = id;
	output->cloning = -1;
	output->engine = engine;
	output->device = device;
	output->tw_output = tw_output_create(engine->display);
	wl_list_init(&output->link);
	if (!output->tw_output) {
		tw_logl_level(TW_LOG_ERRO, "failed to create wl_output");
		return false;
	}

	init_output_state(output);

	tw_signal_setup_listener(&device->events.info,
	                         &output->listeners.info,
	                         notify_output_info);
	tw_signal_setup_listener(&device->events.new_frame,
	                         &output->listeners.frame,
	                         notify_output_frame);
	tw_signal_setup_listener(&device->events.destroy,
	                         &output->listeners.destroy,
	                         notify_output_destroy);
	tw_signal_setup_listener(&device->events.commit_state,
	                         &output->listeners.set_mode,
	                         notify_output_new_mode);

        engine->output_pool |= 1 << id;
        wl_list_init(&output->link);
        wl_list_insert(&engine->heads, &output->link);
        //how this is gonna work?
        if (engine->backend->started) {

        }

        return true;

}
