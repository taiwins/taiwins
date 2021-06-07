/*
 * engine.c - taiwins engine functions
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
#include "options.h"
#include <assert.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#include <ctypes/helpers.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-compat.h>
#include <xkbcommon/xkbcommon.h>
#include <pixman.h>

#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/cursor.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/subsurface.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/gestures.h>

#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include <taiwins/backend.h>
#include <taiwins/engine.h>
#include "utils.h"
#include "internal.h"

static struct tw_engine s_engine = {0};

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_new_input(struct wl_listener *listener, void *data)
{
	struct tw_engine *engine =
		wl_container_of(listener, engine, listeners.new_input);
	struct tw_input_device *device = data;
	struct tw_engine_seat *seat =
		tw_engine_seat_find_create(engine, device->seat_id);
	tw_logl("new input: %s", device->name);
	tw_engine_seat_add_input_device(seat, device);
}

static void
notify_new_output(struct wl_listener *listener, void *data)
{
	struct tw_engine *engine =
		wl_container_of(listener, engine, listeners.new_output);
	struct tw_output_device *device = data;
	tw_logl("new output: %s", device->name);
	tw_engine_new_output(engine, device);
}


static void
notify_engine_release(struct wl_listener *listener, void *data)
{
	struct tw_engine *engine =
		wl_container_of(listener, engine, listeners.display_destroy);

        wl_list_remove(&engine->listeners.display_destroy.link);
	tw_cursor_fini(&engine->global_cursor);
	/* tw_backend_fini_obj_proxy(backend->proxy); */
	engine->started = false;
	engine->display = NULL;
}

static void
notify_engine_backend_started(struct wl_listener *listener, void *data)
{
	struct tw_engine *engine =
		wl_container_of(listener, engine, listeners.backend_started);
	struct tw_backend *backend = data;
	struct tw_render_context *ctx = backend->ctx;

	assert(ctx);
        assert(backend == engine->backend);

        tw_render_context_set_compositor(ctx, &engine->compositor_manager);
        tw_render_context_set_dma(ctx, &engine->dma_engine);
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static bool
engine_init_globals(struct tw_engine *engine)
{
	if (!tw_compositor_init(&engine->compositor_manager,
	                        engine->display))
		return false;
	if (!tw_data_device_manager_init(&engine->data_device_manager,
	                                 engine->display))
		return false;
	if (!tw_linux_dmabuf_init(&engine->dma_engine, engine->display))
		return false;
	if (!tw_presentation_init(&engine->presentation, engine->display))
		return false;
	if (!tw_viewporter_init(&engine->viewporter, engine->display))
		return false;
	if (!tw_gestures_manager_init(&engine->gestures_manager,
	                              engine->display))
		return false;

	tw_layers_manager_init(&engine->layers_manager, engine->display);


	return true;
}


WL_EXPORT struct tw_engine *
tw_engine_create_global(struct wl_display *display, struct tw_backend *backend)
{
	struct tw_engine *engine = &s_engine;
	/* struct tw_engine_obj_proxy *proxy = &s_tw_engine_proxy; */

	if (engine->display) {
		tw_logl_level(TW_LOG_ERRO, "engine already initialized\n");
		return NULL;
	}

	engine->display = display;
	engine->started = false;
	engine->output_pool = 0;
	engine->seat_pool = 0;
	engine->focused_seat = NULL;
	wl_list_init(&engine->heads);
	wl_list_init(&engine->pending_heads);
	wl_list_init(&engine->inputs);

	if (!backend)
		return NULL;
	engine->backend = backend;

	engine->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!engine->xkb_context)
		goto err_context;

	if (!engine_init_globals(engine))
		goto err_globals;

	tw_cursor_init(&engine->global_cursor,
	               &engine->layers_manager.cursor_layer);

	//listeners
	tw_set_display_destroy_listener(display,
	                                &engine->listeners.display_destroy,
	                                notify_engine_release);
	tw_signal_setup_listener(&backend->signals.new_output,
	                         &engine->listeners.new_output,
	                         notify_new_output);
	tw_signal_setup_listener(&backend->signals.new_input,
	                         &engine->listeners.new_input,
	                         notify_new_input);
	tw_signal_setup_listener(&backend->signals.start,
	                         &engine->listeners.backend_started,
	                         notify_engine_backend_started);
	//signals
	wl_signal_init(&engine->signals.seat_created);
	wl_signal_init(&engine->signals.seat_focused);
	wl_signal_init(&engine->signals.seat_remove);
	wl_signal_init(&engine->signals.seat_input);
	wl_signal_init(&engine->signals.output_created);
	wl_signal_init(&engine->signals.output_remove);
	wl_signal_init(&engine->signals.output_resized);

	return engine;
err_globals:
	xkb_context_unref(engine->xkb_context);
err_context:
	return NULL;

}

static struct tw_surface *
try_pick_subsurfaces(struct tw_surface *parent, float x, float y,
                     float *sx, float *sy)
{
	struct tw_surface *surface;
	struct tw_subsurface *sub;
	//recursively picking subsurfaces, we need to find a way to avoid this
	wl_list_for_each(sub, &parent->subsurfaces, parent_link) {
		surface = try_pick_subsurfaces(sub->surface, x, y, sx, sy);
		if (!surface)
			surface = sub->surface;
		if (tw_surface_has_input_point(surface, x, y)) {
			tw_surface_to_local_pos(surface, x, y, sx, sy);
			return surface;
		}
	}
	return NULL;
}

struct tw_surface *
tw_engine_pick_surface_from_layers(struct tw_engine *engine,
                                   float x, float y, float *sx, float *sy)
{
	struct tw_layer *layer;
	struct tw_layers_manager *layers = &engine->layers_manager;
	struct tw_surface *surface, *sub, *picked = NULL;

	SCOPE_PROFILE_BEG();

	//TODO: for very small amount of views, this works well. But it is a
	//linear algorithm so when number of windows gets very large, we may
	//have problems.
	wl_list_for_each(layer, &layers->layers, link) {
		if (layer->position >= TW_LAYER_POS_CURSOR)
			continue;
		wl_list_for_each(surface, &layer->views,
		                 layer_link) {
			if ((sub = try_pick_subsurfaces(surface, x, y,
			                                sx, sy))) {
				picked = sub;
				goto out;
			} else if (tw_surface_has_input_point(surface, x, y)) {
				tw_surface_to_local_pos(surface, x, y, sx, sy);
				picked = surface;
				goto out;
			}
		}
	}
out:
	SCOPE_PROFILE_END();
	if (!picked) {
		*sx = -1000000;
		*sy = -1000000;
	}
	return picked;

}
