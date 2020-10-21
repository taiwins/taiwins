/*
 * output.c - taiwins server wayland backend output implementation
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-egl.h>
#include <wayland-client.h>
#include <wayland-server.h>

#include <taiwins/output_device.h>
#include <taiwins/backend.h>
#include <taiwins/objects/logger.h>
#include <taiwins/render_context.h>
#include <wayland-xdg-shell-client-protocol.h>
#include <taiwins/render_output.h>

#include "internal.h"

static void
wl_commit_output_state(struct tw_output_device *output)
{
	struct tw_wl_output *wl_output =
		wl_container_of(output, wl_output, output.device);
	unsigned width, height;


	assert(output->pending.scale >= 1.0);
	assert(output->pending.current_mode.h > 0 &&
	       output->pending.current_mode.w > 0);
        memcpy(&output->state, &output->pending, sizeof(output->state));

        tw_output_device_raw_resolution(output, &width, &height);

        if (wl_output->egl_window)
	        wl_egl_window_resize(wl_output->egl_window, width, height,
	                             0, 0);
}

static const struct tw_output_device_impl wl_output_impl = {
	.commit_state = wl_commit_output_state,
};

/******************************************************************************
 * xdg toplevel listener
 *****************************************************************************/

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                          int32_t width, int32_t height,
                          struct wl_array *states)
{
	struct tw_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	if (width == 0 || height == 0)
		return;
	tw_output_device_set_custom_mode(&output->output.device,
	                                 width, height, 0);
	tw_output_device_commit_state(&output->output.device);
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct tw_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	tw_wl_output_remove(output);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = handle_toplevel_configure,
	.close = handle_toplevel_close,
};

/******************************************************************************
 * xdg surface listener
 *****************************************************************************/

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
	struct tw_wl_output *output = data;
	assert(output && output->xdg_surface == xdg_surface);
	xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = handle_xdg_surface_configure,
};

/******************************************************************************
 * wl_callback_listener API
 *****************************************************************************/
static const struct wl_callback_listener callback_listener;

static void
handle_callback_done(void *data, struct wl_callback *wl_callback,
                     uint32_t callback_data)

{
	struct tw_wl_output *output = data;

	assert(output->frame == wl_callback);
	wl_callback_destroy(wl_callback);
	wl_signal_emit(&output->output.device.events.new_frame,
	               &output->output.device);
	output->frame = wl_surface_frame(output->wl_surface);
	wl_callback_add_listener(output->frame, &callback_listener, output);
}


static const struct wl_callback_listener callback_listener = {
	.done = handle_callback_done,
};

/******************************************************************************
 * output API
 *****************************************************************************/

void
tw_wl_output_remove(struct tw_wl_output *output)
{
	struct tw_wl_backend *wl = output->wl;

	tw_output_device_fini(&output->output.device);
	tw_render_presentable_fini(&output->output.surface, wl->base.ctx);
	wl_egl_window_destroy(output->egl_window);
	xdg_toplevel_destroy(output->xdg_toplevel);
	xdg_surface_destroy(output->xdg_surface);
	wl_surface_destroy(output->wl_surface);

	free(output);
}

void
tw_wl_output_start(struct tw_wl_output *output)
{
	struct tw_wl_backend *wl = output->wl;
	unsigned width, height;

	assert(wl->base.ctx);
	xdg_toplevel_set_app_id(output->xdg_toplevel, "wlroots");
	xdg_surface_add_listener(output->xdg_surface,
			&xdg_surface_listener, output);
	xdg_toplevel_add_listener(output->xdg_toplevel,
			&xdg_toplevel_listener, output);
	xdg_toplevel_set_app_id(output->xdg_toplevel, "taiwins");
	xdg_toplevel_set_title(output->xdg_toplevel,
	                       output->output.device.name);
	wl_surface_commit(output->wl_surface);

	tw_render_output_set_context(&output->output, wl->base.ctx);
	tw_output_device_commit_state(&output->output.device);
	tw_output_device_raw_resolution(&output->output.device,
	                                &width, &height);

	output->egl_window = wl_egl_window_create(output->wl_surface,
	                                          width, height);
	if (!tw_render_presentable_init_window(&output->output.surface,
	                                       wl->base.ctx,
	                                       output->egl_window)) {
		tw_logl_level(TW_LOG_WARN, "Failed to create render surface "
		              "for wayland output");
		tw_wl_output_remove(output);
	}

	wl_display_roundtrip(wl->remote_display);

	//finally
	wl_signal_emit(&wl->base.events.new_output, &output->output.device);
	wl_signal_emit(&output->output.device.events.info,
	               &output->output.device);

	output->frame = wl_surface_frame(output->wl_surface);
	handle_callback_done(output, output->frame, 0);
}

bool
tw_wl_backend_new_output(struct tw_backend *backend,
                         unsigned width, unsigned height)
{
	struct tw_wl_output *output;
	struct tw_wl_backend *wl = wl_container_of(backend, wl, base);

	if (!wl->globals.compositor || !wl->globals.wm_base)
		return false;
	if (!(output = calloc(1, sizeof(*output))))
		return false;

	output->wl_surface =
		wl_compositor_create_surface(wl->globals.compositor);
	if (!output->wl_surface) {
		tw_logl_level(TW_LOG_ERRO, "Could not create output surface");
		goto err;
	}
	wl_surface_set_user_data(output->wl_surface, output);

        output->xdg_surface =
	        xdg_wm_base_get_xdg_surface(wl->globals.wm_base,
	                                    output->wl_surface);
        if (!output->xdg_surface) {
	        tw_logl_level(TW_LOG_ERRO, "Could not get xdg_surface");
	        goto err_xdgsurface;
        }

        output->xdg_toplevel =
	        xdg_surface_get_toplevel(output->xdg_surface);
        if (!output->xdg_toplevel) {
	        tw_logl_level(TW_LOG_ERRO, "Could not get xdg_toplevel");
	        goto err_toplevel;
        }

	output->curr_pointer = NULL;
	output->wl = wl;
	tw_render_output_init(&output->output, &wl_output_impl);
        tw_output_device_set_custom_mode(&output->output.device, width, height,
                                         0);

	strncpy(output->output.device.make, "wayland",
	        sizeof(output->output.device.make));
	strncpy(output->output.device.model, "wayland",
	        sizeof(output->output.device.model));
	snprintf(output->output.device.name,
	         sizeof(output->output.device.name),
	         "wl_output-%d", wl_list_length(&backend->outputs));

	wl_list_insert(backend->outputs.prev, &output->output.device.link);

	if (backend->started)
		tw_wl_output_start(output);


	return true;
err_toplevel:
	xdg_surface_destroy(output->xdg_surface);
err_xdgsurface:
	wl_surface_destroy(output->wl_surface);
err:
	free(output);
	return false;
}
