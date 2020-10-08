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

#include <GLES2/gl2.h>
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
#include "render_context.h"
#include "render_output.h"

static void
init_engine_output_state(struct tw_engine_output *o)
{
	pixman_rectangle32_t rect = tw_output_device_geometry(o->device);
	//okay, here is what we will need to fix
	wl_list_init(&o->constrain.link);
	pixman_region32_init_rect(&o->constrain.region,
	                          rect.x, rect.y, rect.width, rect.height);

	wl_list_insert(o->engine->global_cursor.constrains.prev,
	               &o->constrain.link);
}

static void
fini_engine_output_state(struct tw_engine_output *o)
{
	tw_reset_wl_list(&o->constrain.link);
	pixman_region32_fini(&o->constrain.region);

}

/******************************************************************************
 * listeners
 *****************************************************************************/

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
	wl_list_remove(&output->listeners.info.link);

	//TODO we should have this
	tw_output_destroy(output->tw_output);

	fini_engine_output_state(output);
	engine->output_pool &= unset;
	wl_signal_emit(&engine->events.output_remove, output);
}

static void
notify_output_info(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.info);
	//TODO: broadcast to tw_output
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
	tw_output_device_set_id(device, id);

	wl_list_init(&output->link);
	if (!output->tw_output) {
		tw_logl_level(TW_LOG_ERRO, "failed to create wl_output");
		return false;
	}

	init_engine_output_state(output);

	tw_signal_setup_listener(&device->events.info,
	                         &output->listeners.info,
	                         notify_output_info);
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
