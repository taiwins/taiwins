/*
 * backend.c - taiwins backend functions
 *
 * Copyright (c) 2019 Xichen Zhou
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

#include <libweston/libweston.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <helpers.h>

#define INCLUDE_BACKEND
#include "taiwins.h"
#include "config.h"

struct tw_backend;

typedef OPTION(struct weston_geometry, geometry) tw_option_geometry;
typedef OPTION(int32_t, scale) tw_option_scale;
typedef OPTION(enum wl_output_transform, transform) tw_option_output_transform;

struct tw_backend_output {
	int id;
	struct tw_backend *backend;
	struct weston_output *output;
	tw_option_geometry pending_geometry;
	tw_option_scale pending_scale;
	tw_option_output_transform pending_transform;
};

struct tw_backend {
	struct weston_compositor *compositor;
	struct tw_config *config;
	enum weston_compositor_backend type;
	//like weston, we have maximum 32 outputs.
	uint32_t output_pool;
	struct tw_backend_output outputs[32];

	struct pixman_region32 region;
	union {
		struct weston_drm_backend_config drm;
		struct weston_wayland_backend_config wayland;
		struct weston_x11_backend_config x11;
	} backend_config;
	struct wl_listener compositor_distroy_listener;
	struct wl_listener windowed_head_changed;
	struct wl_listener drm_head_changed;
	struct wl_listener config_listener;
	struct tw_config_component_listener config_component;
};

static struct tw_backend TWbackend;

struct tw_backend *
tw_backend_get_global(void)
{
	return &TWbackend;
}

/******************************************************************
 * tw_backend_output
 *****************************************************************/

static void
tw_backend_init_output(struct tw_backend *b, struct weston_output *output)
{
	assert(ffs(~b->output_pool) > 0);
	int id = ffs(~b->output_pool) - 1;
	b->output_pool |= 1u << id;
	b->outputs[id].id = id;
	b->outputs[id].output = output;
	b->outputs[id].backend = b;
	b->outputs[id].pending_geometry.valid = false;
	b->outputs[id].pending_scale.valid = false;
	b->outputs[id].pending_transform.valid = false;
}

static void
tw_backend_fini_output(struct tw_backend_output *o)
{
	struct tw_backend *backend = o->backend;
	backend->output_pool &= ~(1u << o->id);
	o->id = 0xffffffff;
	o->output = NULL;
	o->pending_geometry.valid = false;
	o->pending_scale.valid = false;
	o->pending_transform.valid = false;
}

struct tw_backend_output*
tw_backend_output_from_weston_output(struct weston_output *o, struct tw_backend *b)
{
	for (int i = 0; i < 32; i++)
		if (b->outputs[i].output == o)
			return &b->outputs[i];
	return NULL;
}


/******************************************************************
 * head functions
 *****************************************************************/
/* here we just create one output, every thing else attach to it, but later, we
 * can create other outputs */
static void
drm_head_enable(struct weston_head *head, struct weston_compositor *compositor,
		struct tw_backend *backend)
{
	const struct weston_drm_output_api *api =
		weston_drm_output_get_api(compositor);

	//you can convert an output to pending state.
	//try to create the output for it, right now we need to be silly, just
	//use the clone method
	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);

	api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
	api->set_gbm_format(output, NULL);
	api->set_seat(output, NULL);
	weston_output_set_scale(output, 1);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_enable(output);
	tw_backend_init_output(backend, output);

	if (!output->enabled) {
		weston_output_enable(output);
	}
}

static void
drm_head_disable(struct weston_head *head)
{
	struct weston_output *output = weston_head_get_output(head);
	weston_head_detach(head);
	if (wl_list_length(&output->head_list) == 0)
		weston_output_destroy(output);
}

static void
drm_head_check(struct weston_compositor *compositor)
{
	if (!wl_list_length(&compositor->output_list)) {
		struct weston_output *output =
			weston_compositor_create_output(compositor, "taiwins");
		wl_list_remove(&output->link);
		wl_list_insert(&compositor->output_list, &output->link);
	}
}

static void
windowed_head_enable(struct weston_head *head, struct weston_compositor *compositor,
		     struct tw_backend *backend)
{
	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);

	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_set_scale(output, 1);
	api->output_set_size(output, 500, 500);
	if (!output->enabled)
		weston_output_enable(output);
	tw_backend_init_output(backend, output);
}

static void
windowed_head_disabled(struct weston_head *head, struct tw_backend *backend)
{
	struct weston_output *output = weston_head_get_output(head);
	struct tw_backend_output *o = tw_backend_output_from_weston_output(output, backend);
	weston_head_detach(head);
	weston_output_destroy(output);
	tw_backend_fini_output(o);

}

static void
windowed_head_check(struct weston_compositor *compositor)
{
	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);
	if (!wl_list_length(&compositor->output_list)) {
		api->create_head(compositor, "windows");
	}
}

/************************************************************
 * head_listener
 ***********************************************************/
static void
drm_head_changed(struct wl_listener *listener, void *data)
{
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, drm_head_changed);
	struct weston_compositor *compositor = data;
	struct weston_head *head = NULL;
	bool connected, enabled, changed;

	while ((head = weston_compositor_iterate_heads(compositor, head))) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		//shit, it is not connected or enabled
		if (connected && !enabled)
			drm_head_enable(head, compositor, backend);
		else if (enabled && !connected)
			drm_head_disable(head);
		else {
		}
		(void) changed;
		weston_head_reset_device_changed(head);
	}
	drm_head_check(compositor);
}

static void
windowed_head_changed(struct wl_listener *listener, void *data)
{
	//one head one output
	struct weston_compositor *compositor = data;
	struct weston_head *head = NULL;
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, windowed_head_changed);
	bool connected, enabled, changed;

	wl_list_for_each(head, &compositor->head_list, compositor_link) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		if (connected && !enabled) {
			windowed_head_enable(head, compositor, backend);
		} else if (enabled && !connected) {
			windowed_head_disabled(head, backend);
		} else if (enabled && changed) {
			//get the window info and... maybe resize.
		}
		weston_head_reset_device_changed(head);
	}

}

/************************************************************
 * config components
 ***********************************************************/

//we also need to be able to clone someone

static void
backend_apply_lua_config(struct tw_config *c, bool cleanup,
			 struct tw_config_component_listener *listener)
{
	struct weston_output *o;
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    config_component);

	wl_list_for_each(o, &b->compositor->output_list, link) {
		struct tw_backend_output *to =
			tw_backend_output_from_weston_output(o, b);
		bool valid = to->pending_scale.valid ||
			to->pending_geometry.valid ||
			to->pending_transform.valid;
		if (cleanup) {
			to->pending_scale.valid = false;
			to->pending_geometry.valid = false;
			to->pending_transform.valid = false;
			break;
		}
		if (to->pending_scale.valid) {
			o->scale = to->pending_scale.scale;
			//weston prevents us from doing this
			/* weston_output_set_scale( */
			/*	o, to->pending_scale.scale); */
			to->pending_scale.valid = false;
		}
		if (to->pending_transform.valid) {
			weston_output_set_transform(
				o, to->pending_transform.transform);
			to->pending_transform.valid = false;
		}
		if (valid)
			wl_signal_emit(&b->compositor->output_resized_signal, o);
	}
	weston_compositor_schedule_repaint(b->compositor);
}

/************************************************************
 * global functions
 ***********************************************************/

static void
end_backends(struct wl_listener *listener, void *data)
{
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    compositor_distroy_listener);
	pixman_region32_fini(&b->region);
}

static void
setup_backend_listeners(struct tw_backend *b)
{
	struct weston_compositor *compositor = b->compositor;
	wl_list_init(&b->windowed_head_changed.link);
	b->windowed_head_changed.notify = windowed_head_changed;
	wl_list_init(&b->drm_head_changed.link);
	b->drm_head_changed.notify = drm_head_changed;
	//global destroy
	wl_list_init(&b->compositor_distroy_listener.link);
	b->compositor_distroy_listener.notify = end_backends;
	wl_signal_add(&b->compositor->destroy_signal,
		      &b->compositor_distroy_listener);
	wl_list_init(&b->config_component.link);
	//b->config_component.init = backend_init_config_component;
	b->config_component.apply = backend_apply_lua_config;
	tw_config_add_component(b->config, &b->config_component);

	switch (b->type) {
	case WESTON_BACKEND_DRM:
		b->backend_config.drm.base.struct_version =
			WESTON_DRM_BACKEND_CONFIG_VERSION;
		b->backend_config.drm.base.struct_size =
			sizeof(struct weston_drm_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->drm_head_changed);
		break;
	case WESTON_BACKEND_WAYLAND:
		b->backend_config.wayland.base.struct_version =
			WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
		b->backend_config.wayland.base.struct_size =
			sizeof(struct weston_wayland_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->windowed_head_changed);
		break;
	case WESTON_BACKEND_X11:
		b->backend_config.x11.base.struct_version =
			WESTON_X11_BACKEND_CONFIG_VERSION;
		b->backend_config.x11.base.struct_size =
			sizeof(struct weston_x11_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->windowed_head_changed);
		break;
	/* case WESTON_BACKEND_RDP: */
	default:
		//not supported
		assert(0);
	}
}

static const char *const backend_map[] = {
    [WESTON_BACKEND_DRM] = "drm-backend.so",
    [WESTON_BACKEND_FBDEV] = "fbdev-backend.so",
    [WESTON_BACKEND_HEADLESS] = "headless-backend.so",
    [WESTON_BACKEND_RDP] = "rdp-backend.so",
    [WESTON_BACKEND_WAYLAND] = "wayland-backend.so",
    [WESTON_BACKEND_X11] = "x11-backend.so",
};

/**
 * @brief load a weston backend by loading a weston-module
 *
 * This works the same way as `weston_compositor_load_backend` except our
 * modules is not in standard path
 */
static int
load_weston_backend(struct weston_compositor *ec,
                       enum weston_compositor_backend backend,
                       struct weston_backend_config *config_base)
{
	int (*init_backend)(struct weston_compositor *c,
	                    struct weston_backend_config *config_base);

	if (ec->backend) {
		weston_log("Error: a backend is already loaded.\n");
		return -1;
	}
	if (backend > NUMOF(backend_map)) {
		weston_log("Error: backend not supported.\n");
		return -1;
	}

	init_backend = tw_load_weston_module(backend_map[backend],
	                                     "weston_backend_init");
	if (!init_backend) {
		weston_log("Error: backend is not loaded correctly.\n");
		return -1;
	}

	if (init_backend(ec, config_base) < 0) {
		ec->backend = NULL;
		return -1;
	}

	return 0;
}


/************************************************************
 * public functions
 ***********************************************************/
enum weston_compositor_backend
tw_backend_get_type(struct tw_backend *be)
{
	return be->type;
}

void
tw_backend_output_set_scale(struct tw_backend_output *output,
                            unsigned int scale)
{
	output->pending_scale.scale = scale;
	output->pending_scale.valid = true;
}

void
tw_backend_output_set_transform(struct tw_backend_output *output,
                                enum wl_output_transform transform)
{
	output->pending_transform.transform = transform;
	output->pending_transform.valid = true;
}


bool
tw_setup_backend(struct weston_compositor *compositor,
		 struct tw_config *config)
{
	enum weston_compositor_backend backend;
	struct tw_backend *b = tw_backend_get_global();
	compositor->vt_switching = true;
	b->config = config;
	b->compositor = compositor;
	b->output_pool = 0;
	//how to launch an rdp server here?
	if ( getenv("WAYLAND_DISPLAY") != NULL )
		backend = WESTON_BACKEND_WAYLAND;
	else if ( getenv("DISPLAY") != NULL )
		backend = WESTON_BACKEND_X11;
	else
		backend = WESTON_BACKEND_DRM;
	b->type = backend;
	setup_backend_listeners(b);

	load_weston_backend(compositor, backend,
	                    &b->backend_config.drm.base);
	if (backend == WESTON_BACKEND_WAYLAND ||
	    backend == WESTON_BACKEND_X11)
		windowed_head_check(compositor);
	weston_compositor_flush_heads_changed(compositor);

	return true;
}
