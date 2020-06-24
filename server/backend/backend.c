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
#include <objects/seat.h>

#include "backend.h"
#include "backend_internal.h"
#include "objects/compositor.h"
#include "objects/data_device.h"
#include "objects/dmabuf.h"
#include "objects/layers.h"
#include "objects/surface.h"

static struct tw_backend s_tw_backend = {0};
static struct tw_backend_impl s_tw_backend_impl;

/******************************************************************************
 * OUTPUT APIs
 *****************************************************************************/
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
		wl_signal_emit(&backend->output_plug_signal, output);
		tw_backend_commit_output_state(output);
		wl_list_remove(&output->link);
		wl_list_insert(backend->heads.prev, &output->link);
	}
}

void *
tw_backend_get_backend(struct tw_backend *backend)
{
	return backend->auto_backend;
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
	tw_surface_manager_init(&backend->surface_manager);
	tw_layers_manager_init(&backend->layers_manager, backend->display);

	wl_display_init_shm(backend->display);
	wl_display_add_shm_format(backend->display, WL_SHM_FORMAT_ARGB8888);

	return true;
}

static void
release_backend(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_backend *backend =
		container_of(listener, struct tw_backend,
		             display_destroy_listener);

	tw_backend_fini_impl(backend->impl);
	backend->main_renderer = NULL;
	backend->auto_backend = NULL;
	backend->started = false;
	backend->display = NULL;
}

struct tw_backend *
tw_backend_create_global(struct wl_display *display)
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

	if (!tw_backend_init_globals(backend))
		goto err_globals;

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
