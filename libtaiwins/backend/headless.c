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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>

#include <taiwins/backend.h>
#include <taiwins/input_device.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>

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
	struct tw_backend base;

	//this unsigned value could be used by different renderers, now it is
	//only used by gl renderers.
	unsigned int internal_format;
	struct wl_listener display_destroy;

};

struct tw_headless_output {
	struct tw_render_output output;
	struct wl_event_source *timer;
	struct wl_listener present_listener;
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

static int
headless_frame(void *data)
{
	struct tw_headless_output *output = data;

	wl_signal_emit(&output->output.device.events.new_frame,
	               &output->output.device);
	wl_event_source_timer_update(output->timer, 1000000 / (60 * 1000));
	return 0;
}

static void
notify_output_commit(struct wl_listener *listener, void *data)
{
	struct tw_headless_output *output =
		wl_container_of(listener, output, present_listener);
	tw_output_device_present(&output->output.device, NULL);
}

static bool
headless_output_start(struct tw_headless_output *output,
                      struct tw_headless_backend *headless)
{
	uint32_t width, height;
	struct wl_event_loop *loop =
		wl_display_get_event_loop(headless->display);
	struct tw_render_context *ctx = headless->base.ctx;

	tw_signal_setup_listener(&ctx->events.presentable_commit,
	                         &output->present_listener,
	                         notify_output_commit);

	//this will give us good resolution
	tw_render_output_set_context(&output->output, headless->base.ctx);
	tw_output_device_commit_state(&output->output.device);

	tw_output_device_raw_resolution(&output->output.device,
	                                &width, &height);
	tw_render_presentable_init_offscreen(&output->output.surface,
	                                     headless->base.ctx,
	                                     width, height);
	//announce the output.
	wl_signal_emit(&headless->base.events.new_output, output);
	//the wl_output events are ready.
	wl_signal_emit(&output->output.device.events.info, output);

        output->timer = wl_event_loop_add_timer(loop, headless_frame,
                                                  headless);
	wl_event_source_timer_update(output->timer, 1000000 / (60 * 1000));


	return false;
}

static inline void
headless_input_start(struct tw_input_device *device,
                     struct tw_headless_backend *headless)
{
	wl_signal_emit(&headless->base.events.new_input, device);
}

static bool
headless_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_headless_output *output;
	struct tw_input_device *input;
	//checking the externsions
	struct tw_headless_backend *headless =
		wl_container_of(backend, headless, base);

	wl_list_for_each(output, &headless->base.outputs, output.device.link)
		headless_output_start(output, headless);

	wl_list_for_each(input, &headless->base.inputs, link)
		headless_input_start(input, headless);

	return true;
}

static const struct tw_backend_impl headless_impl = {
	.start = headless_start,
	.gen_egl_params = headless_gen_egl_params,
};

static void
headless_commit_output_state(struct tw_output_device *device)
{
	assert(device->pending.scale >= 1.0);
	assert(device->pending.current_mode.h > 0 &&
	       device->pending.current_mode.w > 0);
	memcpy(&device->state, &device->pending, sizeof(device->state));
}

static const struct tw_output_device_impl headless_output_impl = {
	.commit_state = headless_commit_output_state,
};

static void
headless_destroy(struct tw_headless_backend *headless)
{
	struct tw_headless_output *output, *otmp;
	struct tw_input_device *input, *itmp;

	wl_list_for_each_safe(output, otmp, &headless->base.outputs,
	                      output.device.link) {
		tw_render_presentable_fini(&output->output.surface,
		                           headless->base.ctx);
		tw_output_device_fini(&output->output.device);
		wl_event_source_remove(output->timer);
		free(output);
	}

	wl_list_for_each_safe(input, itmp, &headless->base.inputs, link) {
		tw_input_device_fini(input);
		free(input);
	}

	wl_signal_emit(&headless->base.events.destroy, &headless->base);
	free(headless);
}

static void
notify_headless_render_context_destroy(struct wl_listener *listener,
                                       void *data)
{
	struct tw_headless_backend *headless =
		wl_container_of(listener, headless,
		                base.render_context_destroy);
	headless_destroy(headless);
}

static void
notify_headless_display_destroy(struct wl_listener *listener,
                                void *data)
{
	struct tw_headless_backend *headless =
		wl_container_of(listener, headless, display_destroy);
	headless_destroy(headless);
}

struct tw_backend *
tw_headless_backend_create(struct wl_display *display)
{
	struct tw_headless_backend *headless = calloc(1, sizeof(*headless));

        if (!headless)
		return false;
        headless->display = display;
        headless->base.impl = &headless_impl;

        tw_backend_init(&headless->base);

        headless->base.render_context_destroy.notify =
	        notify_headless_render_context_destroy;

        tw_set_display_destroy_listener(headless->display,
                                        &headless->display_destroy,
                                        notify_headless_display_destroy);
        //in both libweston and wlroots, the backend initialize a renderer, it
        //make sense since only the backend knows about the correct renderer
        //options,
        return &headless->base;
}

bool
tw_headless_backend_add_output(struct tw_backend *backend,
                               unsigned int width, unsigned int height)
{
	struct tw_headless_backend *headless =
		wl_container_of(backend->impl, headless, base);
	struct tw_headless_output *output = calloc(1, sizeof(*output));
	struct tw_output_device *device;

        if (output == NULL) {
		tw_logl_level(TW_LOG_ERRO, "failed to create headless output");
		return false;
	}
        device = &output->output.device;
        tw_render_output_init(&output->output, &headless_output_impl);

        tw_output_device_set_custom_mode(&output->output.device,
                                         width, height, 0);
        snprintf(device->name, sizeof(device->name),
                 "headless-output%u", wl_list_length(&headless->base.outputs));
        strncpy(device->make, "headless", sizeof(device->make));
        strncpy(device->model, "headless",sizeof(device->model));
        wl_list_insert(headless->base.outputs.prev, &device->link);

        if (backend->started)
	        headless_output_start(output, headless);

        return true;
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

	tw_input_device_init(device, type, 0, NULL);
	sprintf(device->name, "headless %s",
	        (type == TW_INPUT_TYPE_KEYBOARD) ? "keyboard" :
	        ((type == TW_INPUT_TYPE_POINTER) ? "pointer" : "touch"));
	wl_list_insert(headless->base.inputs.prev, &device->link);

	if (backend->started)
		headless_input_start(device, headless);

	return false;
}
