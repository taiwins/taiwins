/*
 * backend.c - taiwins server drm backend
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
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/types.h>
#include <wayland-server.h>

#include <taiwins/backend.h>
#include <taiwins/backend_drm.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/egl.h>
#include <taiwins/render_context.h>

#include "login/login.h"
#include "internal.h"

static bool
drm_backend_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_drm_display *output;
	struct tw_drm_backend *drm = wl_container_of(backend, drm, base);

	if (tw_drm_init_gbm(drm) == false)
		return false;

	wl_list_for_each(output, &drm->base.outputs, output.device.link)
		tw_drm_display_start(output);
	return true;
}

static const struct tw_egl_options *
drm_gen_egl_params(struct tw_backend *backend)
{
	struct tw_drm_backend *drm = wl_container_of(backend, drm, base);
	//TODO, we just use argb8888 for now
	static struct tw_egl_options egl_opts = {
		.platform = EGL_PLATFORM_GBM_KHR,
		.visual_id = GBM_FORMAT_ARGB8888,
	};
	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	egl_opts.context_attribs = egl_config_attribs;
	egl_opts.native_display = drm->gbm.dev;
	return &egl_opts;
}

static const struct tw_backend_impl drm_impl = {
	.start = drm_backend_start,
	.gen_egl_params = drm_gen_egl_params,
};

static void
drm_backend_stop(struct tw_drm_backend *drm)
{
	struct tw_drm_display *output, *tmp_output;

	wl_list_for_each_safe(output, tmp_output, &drm->base.outputs,
	                      output.device.link)
		tw_drm_display_remove(output);
	wl_signal_emit(&drm->base.events.stop, &drm->base);
	wl_list_remove(&drm->base.render_context_destroy.link);
	drm->base.ctx = NULL;
}

static void
drm_backend_destroy(struct tw_drm_backend *drm)
{
	wl_signal_emit(&drm->base.events.destroy, &drm->base);
	if (drm->base.ctx)
		tw_render_context_destroy(drm->base.ctx);
	tw_login_destroy(drm->login);
	close(drm->gpu_fd);
	free(drm);
}

static void
notify_drm_login_state(struct wl_listener *listener, void *data)
{
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, login_listener);
	struct tw_login *login = data;

	assert(login == drm->login);
	//TODO
}

static void
notify_drm_render_context_destroy(struct wl_listener *listener, void *data)
{
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, base.render_context_destroy);
	drm_backend_stop(drm);
}

static void
notify_drm_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, display_destroy);
	wl_list_remove(&listener->link);
	drm_backend_destroy(drm);
}

struct tw_backend *
tw_drm_backend_create(struct wl_display *display)
{
	struct tw_drm_backend *drm = calloc(1, sizeof(*drm));
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	struct wl_event_source *source;

	if (!drm)
		return NULL;
	tw_backend_init(&drm->base);
	drm->base.impl = &drm_impl;
	drm->crtc_mask = 0;

	drm->login = tw_login_create(display);
	if (!(drm->login))
		goto err_login;

	drm->gpu_fd = tw_login_find_primary_gpu(drm->login);
	if (drm->gpu_fd < 0)
		goto err_gpu_fd;
	tw_drm_print_info(drm->gpu_fd);

	if (!tw_drm_check_features(drm)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to check drm features");
		goto err_features;
	}

	if (!tw_drm_check_resources(drm)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query drm resources");
		goto err_resource;
	}

	source = wl_event_loop_add_fd(loop, drm->gpu_fd, WL_EVENT_READABLE,
	                              tw_drm_handle_drm_event, drm);
	if (!source) {
		tw_logl_level(TW_LOG_ERRO, "Failed to monitor drm events");
		goto err_event;
	}

	//add listeners
	tw_signal_setup_listener(&drm->login->events.attributes_change,
	                         &drm->login_listener,
	                         notify_drm_login_state);
	tw_set_display_destroy_listener(display, &drm->display_destroy,
	                                notify_drm_display_destroy);
	drm->base.render_context_destroy.notify =
		notify_drm_render_context_destroy;

	return &drm->base;
err_event:
err_resource:
err_features:
	close(drm->gpu_fd);

err_gpu_fd:
	tw_login_destroy(drm->login);
err_login:
	free(drm);
	return NULL;
}

struct tw_login *
tw_drm_backend_get_login(struct tw_backend *base)
{
	struct tw_drm_backend *drm = NULL;

        assert(base->impl == &drm_impl);
	drm = wl_container_of(base, drm, base);
	return drm->login;
}
