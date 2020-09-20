/*
 * backend.c - taiwins backend functions
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

#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/render/wlr_renderer.h>

#include <ctypes/helpers.h>
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
#include <taiwins/objects/profiler.h>

#include "profiling.h"
#include "backend.h"
#include "backend_internal.h"

static struct tw_backend s_tw_backend = {0};
static struct tw_backend_impl s_tw_backend_impl;

/******************************************************************************
 * BACKEND APIs
 *****************************************************************************/

void
tw_backend_defer_outputs(struct tw_backend *backend, bool defer)
{
	backend->defer_output_creation = defer;
}

void
tw_backend_flush(struct tw_backend *backend)
{
	struct tw_backend_output *output, *tmp;
	//this will not guarantee the the heads
	if (!backend->started)
		wlr_backend_start(backend->auto_backend);
	backend->started = true;

	wl_list_for_each_safe(output, tmp, &backend->pending_heads, link) {
		tw_backend_commit_output_state(output);
		wl_signal_emit(&backend->output_plug_signal, output);
		wl_list_remove(&output->link);
		wl_list_insert(backend->heads.prev, &output->link);
	}
}

void *
tw_backend_get_backend(struct tw_backend *backend)
{
	return backend->auto_backend;
}

struct tw_backend_output *
tw_backend_focused_output(struct tw_backend *backend)
{
	struct tw_seat *seat;
	struct wl_resource *wl_surface = NULL;
	struct tw_surface *tw_surface = NULL;
	struct tw_backend_seat *backend_seat;

	if (wl_list_length(&backend->heads) == 0)
		return NULL;

	wl_list_for_each(backend_seat, &backend->inputs, link) {
		struct tw_pointer *pointer;
		struct tw_keyboard *keyboard;
		struct tw_touch *touch;

		seat = backend_seat->tw_seat;
		if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) {
			pointer = &seat->pointer;
			wl_surface = pointer->focused_surface;
			tw_surface = (wl_surface) ?
				tw_surface_from_resource(wl_surface) : NULL;
			if (tw_surface)
				return &backend->outputs[tw_surface->output];
		}
		else if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
			keyboard = &seat->keyboard;
			wl_surface = keyboard->focused_surface;
			tw_surface = (wl_surface) ?
				tw_surface_from_resource(wl_surface) : NULL;
			if (tw_surface)
				return &backend->outputs[tw_surface->output];
		} else if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) {
			touch = &seat->touch;
			wl_surface = touch->focused_surface;
			tw_surface = (wl_surface) ?
				tw_surface_from_resource(wl_surface) : NULL;
			if (tw_surface)
				return &backend->outputs[tw_surface->output];
		}
	}

	struct tw_backend_output *head;
	wl_list_for_each(head, &backend->heads, link)
		return head;

	return NULL;
}

struct tw_backend_output *
tw_backend_output_from_cursor_pos(struct tw_backend *backend)
{
	pixman_region32_t *output_region;
	struct tw_backend_output *output;
	wl_list_for_each(output, &backend->heads, link) {
		if (output->cloning >= 0)
			continue;
		output_region = &output->state.constrain.region;
		if (pixman_region32_contains_point(output_region,
		                                   backend->global_cursor.x,
		                                   backend->global_cursor.y,
		                                   NULL))
			return output;
	}
	return NULL;
}

struct tw_backend_output *
tw_backend_output_from_resource(struct wl_resource *resource)
{
	struct wlr_output *wlr_output = wlr_output_from_resource(resource);
	return wlr_output->data;
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
		if (!surface) {
			surface = sub->surface;
			if (tw_surface_has_input_point(surface, x, y)) {
				tw_surface_to_local_pos(surface, x, y, sx, sy);
				return surface;
			}
		}
	}
	return NULL;
}

struct tw_surface *
tw_backend_pick_surface_from_layers(struct tw_backend *backend,
                                    float x, float y,
                                    float *sx,  float *sy)
{
	struct tw_layer *layer;
	struct tw_layers_manager *layers = &backend->layers_manager;
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

static bool
tw_backend_init_globals(struct tw_backend *backend)
{
	if (!tw_compositor_init(&backend->compositor_manager,
	                        backend->display))
		return false;
	if (!tw_data_device_manager_init(&backend->data_device_manager,
	                                 backend->display))
		return false;
	if (!tw_linux_dmabuf_init(&backend->dma_engine, backend->display))
		return false;
	if (!tw_presentation_init(&backend->presentation, backend->display))
		return false;
	if (!tw_viewporter_init(&backend->viewporter, backend->display))
		return false;

	tw_surface_manager_init(&backend->surface_manager);
	tw_layers_manager_init(&backend->layers_manager, backend->display);

	return true;
}

static void
release_backend(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_backend *backend =
		container_of(listener, struct tw_backend,
		             display_destroy_listener);

	tw_cursor_fini(&backend->global_cursor);
	tw_backend_fini_impl(backend->impl);
	backend->main_renderer = NULL;
	backend->auto_backend = NULL;
	backend->started = false;
	backend->display = NULL;
}

struct tw_backend *
tw_backend_create_global(struct wl_display *display,
                         wlr_renderer_create_func_t render_create)
{
	struct tw_backend *backend = &s_tw_backend;
	struct tw_backend_impl *impl = &s_tw_backend_impl;

	assert(!impl->backend);

	if (backend->display) {
		tw_logl("EE: taiwins backend already initialized\n");
		return NULL;
	}

	backend->display = display;
	backend->started = false;
	backend->output_pool = 0;
	backend->seat_pool = 0;

	backend->auto_backend = wlr_backend_autocreate(display, render_create);
	if (!backend->auto_backend)
		goto err;

	backend->main_renderer =
		wlr_backend_get_renderer(backend->auto_backend);
	//this would initialize the wl_shm
	wlr_renderer_init_wl_display(backend->main_renderer, display);

	backend->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!backend->xkb_context)
		goto err_context;

	if (!tw_backend_init_globals(backend))
		goto err_globals;

	// initialize the global cursor, every seat will register the events on
	// it
	tw_cursor_init(&backend->global_cursor,
	               &backend->layers_manager.cursor_layer);

	wl_list_init(&backend->display_destroy_listener.link);
	backend->display_destroy_listener.notify = release_backend;
	wl_display_add_destroy_listener(display,
	                                &backend->display_destroy_listener);

	wl_signal_init(&backend->output_frame_signal);
	wl_signal_init(&backend->output_plug_signal);
	wl_signal_init(&backend->output_unplug_signal);
	wl_signal_init(&backend->seat_add_signal);
	wl_signal_init(&backend->seat_rm_signal);
	wl_signal_init(&backend->seat_ch_signal);

	wl_list_init(&backend->inputs);
        wl_list_init(&backend->heads);
        wl_list_init(&backend->pending_heads);

        //output
        tw_backend_init_impl(impl, backend);
	return backend;
err_globals:
	xkb_context_unref(backend->xkb_context);
err_context:
	wlr_backend_destroy(backend->auto_backend);
	backend->auto_backend = NULL;
err:
	return NULL;
}

void
tw_backend_add_listener(struct tw_backend *backend,
                        enum tw_backend_event_type event,
                        struct wl_listener *listener)
{
	switch (event) {
	case TW_BACKEND_ADD_OUTPUT:
		wl_signal_add(&backend->output_plug_signal, listener);
		break;
	case TW_BACKEND_RM_OUTPUT:
		wl_signal_add(&backend->output_unplug_signal, listener);
		break;
	case TW_BACKEND_ADD_SEAT:
		wl_signal_add(&backend->seat_add_signal, listener);
		break;
	case TW_BACKEND_RM_SEAT:
		wl_signal_add(&backend->seat_rm_signal, listener);
		break;
	case TW_BACKEND_CH_SEAT:
		wl_signal_add(&backend->seat_ch_signal, listener);
		break;
	}
}

void
tw_backend_switch_session(struct tw_backend *backend, uint32_t id)
{
	struct wlr_session *session;

	if (id > 0 && id <= 7 && wlr_backend_is_multi(backend->auto_backend)) {
		session = wlr_backend_get_session(backend->auto_backend);
		if (session)
			wlr_session_change_vt(session, id);
	}
}

static void
surface_add_to_outputs_list(struct tw_backend *backend,
                            struct tw_surface *surface)
{
	struct tw_backend_output *output;
	struct tw_subsurface *sub;

	assert(surface->output >= 0 && surface->output <= 31);
	output = &backend->outputs[surface->output];

	wl_list_insert(output->views.prev,
	               &surface->links[TW_VIEW_OUTPUT_LINK]);

	wl_list_for_each(sub, &surface->subsurfaces, parent_link)
		surface_add_to_outputs_list(backend, sub->surface);
}

static void
subsurface_add_to_list(struct wl_list *parent_head, struct tw_surface *surface)
{
	struct tw_subsurface *sub;

	wl_list_insert(parent_head->prev,
	               &surface->links[TW_VIEW_GLOBAL_LINK]);
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link) {
		subsurface_add_to_list(&surface->links[TW_VIEW_GLOBAL_LINK],
		                       sub->surface);
	}
}

static void
surface_add_to_list(struct tw_backend *backend, struct tw_surface *surface)
{
	//we should also add to the output
	struct tw_subsurface *sub;
	struct tw_layers_manager *manager = &backend->layers_manager;

	wl_list_insert(manager->views.prev,
	               &surface->links[TW_VIEW_GLOBAL_LINK]);

	//subsurface inserts just above its main surface, here we take the
	//reverse order of the subsurfaces and insert them one by one in front
	//of the main surface
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link)
		subsurface_add_to_list(&surface->links[TW_VIEW_GLOBAL_LINK],
		                       sub->surface);
}

void
tw_backend_build_surface_list(struct tw_backend *backend)
{
	struct tw_surface *surface;
	struct tw_layer *layer;
	struct tw_backend_output *output;
	struct tw_layers_manager *manager = &backend->layers_manager;

	SCOPE_PROFILE_BEG();

	wl_list_init(&manager->views);
	wl_list_for_each(output, &backend->heads, link)
		wl_list_init(&output->views);

	wl_list_for_each(layer, &manager->layers, link) {
		wl_list_for_each(surface, &layer->views, layer_link) {
			surface_add_to_list(backend, surface);
			surface_add_to_outputs_list(backend, surface);
		}
	}

	SCOPE_PROFILE_END();
}
