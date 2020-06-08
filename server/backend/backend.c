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
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/render/wlr_renderer.h>

#include <ctypes/helpers.h>
#include <xkbcommon/xkbcommon-compat.h>
#include <xkbcommon/xkbcommon.h>

#include <taiwins.h>
#include <seat/seat.h>

#include "backend.h"
#include "backend_internal.h"

static struct tw_backend  s_tw_backend = {0};

/******************************************************************************
 * OUTPUT APIs
 *****************************************************************************/
static struct wlr_output_mode *
pick_output_mode(struct tw_backend_output *o, struct wlr_output *output)
{
	int32_t min_refresh = -1;
	struct wlr_output_mode *mode = NULL, *candidate = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == o->state.w &&
		    mode->height == o->state.h &&
		    mode->refresh == o->state.refresh)
			return mode;
		//if we have multiple mode with same resolution, pick the one
		//with highest refresh rate.
		else if (mode->width == o->state.w &&
		         mode->height == o->state.h &&
		         mode->refresh > min_refresh)
			candidate = mode;
	}
	if (candidate)
		return candidate;
	else
		return wlr_output_preferred_mode(output);
}

static void
commit_output_state(struct tw_backend_output *o)
{
	struct wlr_output *output = o->wlr_output;
	struct wlr_output_mode *mode;

	if (o->state.dirty) {
		wlr_output_enable(output, o->state.activate);
		wlr_output_set_transform(output, o->state.transform);
		wlr_output_set_scale(output, o->state.scale);

		mode = (o->state.preferred_mode) ?
			wlr_output_preferred_mode(output) :
			pick_output_mode(o, output);
		wlr_output_set_mode(output, mode);

		wlr_output_commit(output);
		o->state.dirty = false;

		//now here we can decide if we want to implement wlr_output_management protocol
	}
}

static void
init_output_state(struct tw_backend_output *o)
{
	o->state.dirty = true;
	o->state.activate = true;
	o->state.scale = 1.0;
	o->state.transform = WL_OUTPUT_TRANSFORM_NORMAL;
	o->state.preferred_mode = true;
	//okay, here is what we will need to fix
	o->state.x = 0; o->state.y = 0;
}

static void
notify_new_output_frame(struct wl_listener *listener, void *data)
{
	struct tw_backend_output *output =
		container_of(listener, struct tw_backend_output,
		             frame_listener);
	struct tw_backend *backend = output->backend;

	wl_signal_emit(&backend->output_frame_signal, output);
}

static void
notify_output_remove(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_backend_output *output =
		container_of(listener, struct tw_backend_output,
		             destroy_listener);
	struct tw_backend *backend = output->backend;
	/* struct wlr_backend *wlr_backend = backend->auto_backend; */
	uint32_t unset = ~(1 << output->id);

	output->id = -1;
	wl_list_remove(&output->link);

	backend->output_pool &= unset;
	wl_signal_emit(&backend->output_unplug_signal, output);

	//TODO if is windowed output, we shall just quit, it can be done as a
	//signal as well.
}

static void
notify_new_output(struct wl_listener *listener, void *data)
{
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, head_add_listener);
	struct wlr_output *wlr_output = data;
	uint32_t id = ffs(backend->output_pool);
	struct tw_backend_output *output = &backend->outputs[id];

	wlr_output->data = output;
	output->id = id;
	output->backend = backend;
	output->wlr_output = wlr_output;
	wl_list_init(&output->frame_listener.link);
	wl_list_init(&output->destroy_listener.link);
	output->frame_listener.notify = notify_new_output_frame;
	output->destroy_listener.notify = notify_output_remove;
	wl_signal_add(&wlr_output->events.frame, &output->frame_listener);
	wl_signal_add(&wlr_output->events.destroy, &output->destroy_listener);

	//setup default state
	init_output_state(output);

	//setup on backend side
        backend->output_pool |= 1 << id;
	wl_list_init(&output->link);
	if (backend->defer_output_creation)
		wl_list_insert(backend->pending_heads.prev, &output->link);
	else
		wl_list_insert(backend->heads.prev, &output->link);

	//tell the clients
        if (!backend->defer_output_creation) {
	        wl_signal_emit(&backend->output_plug_signal, output);
	        commit_output_state(output);
        }
}

struct tw_backend_output *
tw_backend_find_output(struct tw_backend *backend, const char *name)
{
	struct tw_backend_output *output = NULL;

	wl_list_for_each(output, &backend->heads, link) {
		if (strncasecmp(output->wlr_output->name, name, 32) == 0)
			return output;
	}
	wl_list_for_each(output, &backend->pending_heads, link) {
		if (strncasecmp(output->wlr_output->name, name, 32) == 0)
			return output;
	}
	return output;
}

void
tw_backend_set_output_scale(struct tw_backend_output *output, float scale)
{
	if (scale > 0.5 && scale < 4.0) {
		output->state.scale = scale;
		output->state.dirty = true;
	}
}

void
tw_backend_set_output_transformation(struct tw_backend_output *output,
                                     enum wl_output_transform transform)
{
	output->state.transform = transform;
	output->state.dirty = true;
}

int
tw_backend_get_output_modes(struct tw_backend_output *output,
                            struct tw_backend_output_mode *modes)
{
	struct wlr_output_mode *mode;
	int i = 0;
	int n_modes = wl_list_length(&output->wlr_output->modes);

	if (modes) {
		wl_list_for_each(mode, &output->wlr_output->modes, link) {
			modes[i].w = mode->width;
			modes[i].h = mode->height;
			modes[i].refresh = mode->refresh;
			i++;
		}
	}
	return n_modes;
}

void
tw_backend_set_output_mode(struct tw_backend_output *output,
                           const struct tw_backend_output_mode *mode)
{
	output->state.w = mode->w;
	output->state.h = mode->h;
	output->state.refresh = mode->refresh;
	output->state.dirty = true;
}

void
tw_backend_set_output_position(struct tw_backend_output *output,
                               uint32_t x, uint32_t y)
{
	output->state.x = x;
	output->state.y = y;
	output->state.dirty = true;
}

void
tw_backend_output_clone(struct tw_backend_output *dst,
                        const struct tw_backend_output *src)
{
	//TODO there are a few problems with this, this method will not work
	//well.

	//1. If src changes after this function, do we change the dst as well?

        //2. If src was destroyed, how do we deal with the clone?

	//3. What if there is a cascading setup?
	dst->cloning = src->id;
	dst->state.x = src->state.x;
	dst->state.y = src->state.x;
	dst->state.w = src->state.w;
	dst->state.h = src->state.h;
	dst->state.refresh = src->state.refresh;
	dst->state.scale = src->state.scale;
	dst->state.transform = src->state.transform;
	dst->state.preferred_mode = src->state.preferred_mode;
	dst->state.gamma_value = src->state.gamma_value;
	dst->state.activate = src->state.activate;

	dst->state.dirty = true;
}

void
tw_backend_output_enable(struct tw_backend_output *output,
                         bool enable)
{
	output->state.activate = enable;
	output->state.dirty = true;
}

//gamma or color temperature ?
void
tw_backend_output_set_gamma(struct tw_backend_output *output,
                            float gamma)
{
	if (gamma > 0.5 && gamma < 2.2)
		output->state.gamma_value = gamma;
	//TODO: having gamma with effect also
	//output->state.dirty = true;
}

/******************************************************************************
 * INPUT APIs
 *****************************************************************************/
static struct tw_backend_seat *
find_seat_missing_dev(struct tw_backend *backend,
                      struct wlr_input_device *dev,
                      enum tw_input_device_cap cap)
{
	struct tw_backend_seat *seat;

	wl_list_for_each(seat, &backend->inputs, link) {
		if (!(seat->capabilities & cap))
			return seat;
	}

	return NULL;
}

static struct tw_backend_seat *
new_seat_for_backend(struct tw_backend *backend,
                     struct wlr_input_device *dev)
{
	struct tw_backend_seat *seat;
	int new_seat_id = ffs(backend->seat_pool);
	if (new_seat_id >= 8)
		return NULL;

	// init the seat
	seat = &backend->seats[new_seat_id];
	seat->backend = backend;
	seat->idx = new_seat_id;
	seat->capabilities = 0;
	seat->tw_seat = tw_seat_create(backend->display, dev->name);

	wl_list_init(&seat->link);
	// setup the backend side
	backend->seat_pool |= (1 << new_seat_id);
	wl_list_insert(backend->inputs.prev, &seat->link);
	return seat;
}

struct tw_backend_seat *
tw_backend_seat_find_create(struct tw_backend *backend,
                            struct wlr_input_device *dev,
                            enum tw_input_device_cap cap)
{
	struct tw_backend_seat *seat =
		find_seat_missing_dev(backend, dev, cap);
	if (!seat) {
		seat = new_seat_for_backend(backend, dev);
		wl_signal_emit(&backend->seat_add_signal, seat);
	}
	if (!seat)
		return NULL;

	return seat;
}

void
tw_backend_seat_destroy(struct tw_backend_seat *seat)
{
	uint32_t unset = ~(1 << seat->idx);

	wl_signal_emit(&seat->backend->seat_rm_signal, seat);

	wl_list_remove(&seat->link);
	seat->idx = -1;
	tw_seat_destroy(seat->tw_seat);
	seat->tw_seat = NULL;

	seat->backend->seat_pool &= unset;
}


void *
tw_backend_seat_get_backend(struct tw_backend_seat *seat)
{
	return seat->tw_seat;
}

void
tw_backend_seat_set_xkb_rules(struct tw_backend_seat *seat,
                              struct xkb_rule_names *rules)
{
	//TODO
}

static void
notify_new_input(struct wl_listener *listener, void *data)
{
	//TODO we can defer for input device as well.
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, input_add_listener);
	struct wlr_input_device *dev = data;

	switch (dev->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		tw_backend_new_keyboard(backend, dev);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		tw_backend_new_pointer(backend, dev);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		tw_backend_new_touch(backend, dev);
		break;
	case WLR_INPUT_DEVICE_SWITCH:
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		break;
	default:
		break;
	}
}

/*******************************************************************************
 * BACKEND APIs
 ******************************************************************************/

static void
release_backend(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_backend *backend =
		container_of(listener, struct tw_backend,
		             compositor_destroy_listener);

	backend->main_renderer = NULL;
	backend->auto_backend = NULL;
	backend->started = false;
	backend->display = NULL;
}

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
		wl_signal_emit(&backend->output_plug_signal, output);
		commit_output_state(output);
		wl_list_remove(&output->link);
		wl_list_insert(backend->heads.prev, &output->link);
	}
}

void *
tw_backend_get_backend(struct tw_backend *backend)
{
	return backend->auto_backend;
}

struct tw_backend *
tw_backend_create_global(struct wl_display *display)
{
	struct tw_backend *backend = &s_tw_backend;

	if (backend->display) {
		tw_logl("EE: taiwins backend already initialized\n");
		return NULL;
	}

	backend->display = display;
	backend->started = false;
	backend->output_pool = 0;
	backend->seat_pool = 0;

	backend->auto_backend = wlr_backend_autocreate(display, NULL);
	if (!backend->auto_backend)
		goto err;

	backend->main_renderer =
		wlr_backend_get_renderer(backend->auto_backend);

	backend->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!backend->xkb_context)
		goto err_context;
	// initialize the global cursor, every seat will register the events on
	// it
	backend->global_cursor = wlr_cursor_create();
	if (!backend->global_cursor)
		goto err_cursor;

	wl_list_init(&backend->compositor_destroy_listener.link);
	backend->compositor_destroy_listener.notify = release_backend;
	wl_display_add_destroy_listener(display,
	                                &backend->compositor_destroy_listener);

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
        wl_list_init(&backend->head_add_listener.link);
        backend->head_add_listener.notify = notify_new_output;
        wl_signal_add(&backend->auto_backend->events.new_output,
                      &backend->head_add_listener);
        //input
        wl_list_init(&backend->input_add_listener.link);
        backend->input_add_listener.notify = notify_new_input;
        wl_signal_add(&backend->auto_backend->events.new_input,
                      &backend->input_add_listener);

	return backend;

err_cursor:
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
