/*
 * headless.c - taiwins server headless backend implementation
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


#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>
#include <backend.h>

#include "input_device.h"
#include "output_device.h"
#include "egl.h"
#include "render_context.h"

/******************************************************************************
 * headless backend implementation
 *****************************************************************************/

/**
 * @brief a headless backend holds output as in memory framebuffers.
 *
 * The Contents are not displayed but users can read pixels from it thus makes
 * it good for debuging/testing purpose.
 *
 */
struct tw_headless_backend {
	struct wl_display *display;
	struct tw_backend *base;
	struct tw_backend_impl impl;

	//this unsigned value could be used by different renderers, now it is
	//only used by gl renderers.
	unsigned int internal_format;

	struct wl_list outputs;
	struct wl_list inputs;
	struct wl_listener display_destroy;

};

static const struct tw_egl_options *
headless_gen_egl_params(struct tw_backend *backend)
{
	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_NONE,
	};

	static struct tw_egl_options egl_opts = {
		.platform = EGL_PLATFORM_SURFACELESS_MESA,
		.native_display = EGL_DEFAULT_DISPLAY,
		.visual_id = 0,
		//TODO missing drm formats
		.context_attribs = (EGLint *)egl_config_attribs,
	};

	return &egl_opts;
}

static bool
headless_output_start(struct tw_output_device *output,
                      struct tw_headless_backend *headless)
{
	//announce the output.
	wl_signal_emit(&headless->impl.events.new_output, output);
	//the wl_output events are ready.
	wl_signal_emit(&output->events.info, output);

	return false;
}

static inline void
headless_input_start(struct tw_input_device *device,
                     struct tw_headless_backend *headless)
{
	wl_signal_emit(&headless->impl.events.new_input, device);
}

static bool
headless_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_output_device *output;
	struct tw_input_device *input;
	//checking the externsions
	struct tw_headless_backend *headless =
		wl_container_of(backend->impl, headless, impl);

	wl_list_for_each(output, &headless->outputs, link)
		headless_output_start(output, headless);

	wl_list_for_each(input, &headless->inputs, link)
		headless_input_start(input, headless);

	return true;
}

static void
notify_headless_display_destroy(struct wl_listener *listener,
                                void *data)
{
	struct tw_output_device *output, *output_tmp;
	struct tw_input_device *input, *input_tmp;

	struct tw_headless_backend *headless =
		wl_container_of(listener, headless, display_destroy);

	wl_list_for_each_safe(output, output_tmp, &headless->outputs, link) {
		tw_output_device_fini(output);
		free(output);
	}

	wl_list_for_each_safe(input, input_tmp, &headless->inputs, link) {
		tw_input_device_fini(input);
		free(input);
	}

	wl_signal_emit(&headless->impl.events.destroy, &headless->impl);
	free(headless);
}

struct tw_backend_impl *
tw_headless_backend_create(struct wl_display *display,
                           struct tw_backend *backend)
{
	struct tw_headless_backend *headless = calloc(1, sizeof(*headless));

        if (!headless)
		return false;
        headless->display = display;
        headless->base = backend;
        headless->impl.gen_egl_params = headless_gen_egl_params;
        headless->impl.start = headless_start;

        wl_list_init(&headless->outputs);
        wl_list_init(&headless->inputs);

        wl_signal_init(&headless->impl.events.destroy);
        wl_signal_init(&headless->impl.events.new_input);
        wl_signal_init(&headless->impl.events.new_output);

        tw_set_display_destroy_listener(headless->display,
                                        &headless->display_destroy,
                                        notify_headless_display_destroy);

        //in both libweston and wlroots, the backend initialize a renderer, it
        //make sense since only the backend knows about the correct renderer
        //options,
        return &headless->impl;
}

bool
tw_headless_backend_add_output(struct tw_backend *backend,
                               unsigned int width, unsigned int height)
{
	struct tw_headless_backend *headless =
		wl_container_of(backend->impl, headless, base);
	struct tw_output_device *output = calloc(1, sizeof(*output));

        if (output == NULL) {
		tw_logl_level(TW_LOG_ERRO, "failed to create headless output");
		return false;
	}

        tw_output_device_init(output);
        snprintf(output->name, sizeof(output->name), "headless-output%u",
                wl_list_length(&headless->outputs));
        strncpy(output->make, "headless", sizeof(output->make));
        strncpy(output->model, "headless", sizeof(output->model));
        wl_list_insert(headless->outputs.prev, &output->link);

        if (backend->started)
	        headless_output_start(output, headless);

        return false;
}

bool
tw_headless_backend_add_input_device(struct tw_backend *backend,
                                     enum tw_input_device_type type)
{
	struct tw_headless_backend *headless =
		wl_container_of(backend->impl, headless, base);
	struct tw_input_device *device = calloc(1, sizeof(*device));
	if (!device)
		return false;

	tw_input_device_init(device, type, NULL);

	wl_list_insert(headless->inputs.prev, &device->link);

	if (backend->started)
		headless_input_start(device, headless);

	return false;
}
