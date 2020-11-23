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

extern const struct tw_drm_gpu_impl tw_gpu_gbm_impl;
extern const struct tw_drm_gpu_impl tw_gpu_stream_impl;

static bool
drm_backend_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_drm_display *output;
	struct tw_drm_backend *drm = wl_container_of(backend, drm, base);

	wl_list_for_each(output, &drm->base.outputs, output.device.link) {
		if (output->status.connected)
			tw_drm_display_start(output);
	}
	return true;
}

static const struct tw_egl_options *
drm_gen_egl_params(struct tw_backend *backend)
{
	struct tw_drm_backend *drm = wl_container_of(backend, drm, base);
	struct tw_drm_gpu *boot_gpu = drm->boot_gpu;

	return boot_gpu->impl->gen_egl_params(boot_gpu);
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

static inline void
drm_backend_release_gpus(struct tw_drm_backend *drm)
{
	struct tw_drm_gpu *gpu;
	wl_array_for_each(gpu, &drm->gpus) {
		// are releasing gpu resources here but the outputs are
		// destroyed already in `drm_backend_stop`.
		wl_event_source_remove(gpu->event);
		tw_drm_free_gpu_resources(gpu);
		gpu->impl->free_gpu_device(gpu);
		tw_login_close(drm->login, gpu->gpu_fd);
	}
	wl_array_release(&drm->gpus);
}

static void
drm_backend_destroy(struct tw_drm_backend *drm)
{
	wl_signal_emit(&drm->base.events.destroy, &drm->base);
	if (drm->base.ctx)
		tw_render_context_destroy(drm->base.ctx);
	drm_backend_release_gpus(drm);
	tw_login_destroy(drm->login);
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

static bool
drm_backend_init_gpu(struct tw_drm_gpu *gpu, struct tw_login_gpu *login_gpu,
                     struct tw_drm_backend *drm)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(drm->display);

	gpu->drm = drm;
	gpu->gpu_fd = login_gpu->fd;
	gpu->sysnum = login_gpu->sysnum;
	gpu->boot_vga = login_gpu->boot_vga;
	gpu->crtc_mask = 0;
	wl_list_init(&gpu->crtc_list);
	wl_list_init(&gpu->plane_list);
#if _TW_BUILD_EGLSTREAM
	const char *platform = getenv("TW_DRM_PLATFORM");
	if (platform && !strcmp(platform, _DRM_PLATFORM_STREAM))
		gpu->impl = &tw_gpu_stream_impl;
	else
#endif
		gpu->impl = &tw_gpu_gbm_impl;

	if (!tw_drm_check_gpu_features(gpu)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to check features "
		              "for gpu-%d", gpu->gpu_fd);
		goto err_features;
	}
	if (!tw_drm_check_gpu_resources(gpu)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query drm"
		              "resources for gpu-%d", gpu->gpu_fd);
		goto err_resources;
	}
	if (!gpu->impl->get_gpu_device(gpu, login_gpu))
		goto err_handle;

	gpu->event = wl_event_loop_add_fd(loop, gpu->gpu_fd,
	                                  WL_EVENT_READABLE,
	                                  tw_drm_handle_drm_event,
	                                  gpu);
	if (!gpu->event) {
		tw_logl_level(TW_LOG_ERRO, "Failed to monitor events "
		              "for gpu-%d", gpu->gpu_fd);
		goto err_event;
	}
	gpu->activated = true;
	return true;

err_event:
err_handle:
	tw_drm_free_gpu_resources(gpu);
err_resources:
err_features:
	gpu->activated = false;
	return false;
}

static bool
drm_backend_collect_gpus(struct tw_drm_backend *drm)
{
	int ngpus = 0;
	struct tw_login_gpu login_gpus[16] = {0};
	struct tw_drm_gpu *gpus = NULL;

	wl_array_init(&drm->gpus);

	ngpus = tw_login_find_gpus(drm->login, 16, login_gpus);
	if (ngpus <= 0)
		return false;

	gpus = wl_array_add(&drm->gpus, ngpus * sizeof(struct tw_drm_gpu));
	if (!gpus)
		goto err_array;

	for (int i = 0; i < ngpus; i++)
		drm_backend_init_gpu(&gpus[i], &login_gpus[i], drm);
	//check if boot_vga works
	for (int i = 0; i < ngpus; i++)
		if (gpus[i].boot_vga == true && gpus[i].activated == false)
			return false;
	return true;

err_array:
	for (int i = 0; i < ngpus; i++)
		tw_login_close(drm->login, login_gpus[i].fd);
	return false;
}

static inline struct tw_drm_gpu *
drm_backend_get_boot_gpu(struct tw_drm_backend *drm)
{
	struct tw_drm_gpu *gpu;

	wl_array_for_each(gpu, &drm->gpus)
		if (gpu->boot_vga)
			return gpu;
	return NULL;
}

struct tw_backend *
tw_drm_backend_create(struct wl_display *display)
{
	struct tw_drm_backend *drm = calloc(1, sizeof(*drm));

	if (!drm)
		return NULL;
	tw_backend_init(&drm->base);
	drm->display = display;
	drm->base.impl = &drm_impl;
	wl_array_init(&drm->gpus);

	drm->login = tw_login_create(display);
	if (!(drm->login))
		goto err_login;
	if (!drm_backend_collect_gpus(drm))
		goto err_collect_gpus;
	drm->boot_gpu = drm_backend_get_boot_gpu(drm);
	if (!drm->boot_gpu)
		goto err_boot_gpu;

	//add listeners
	tw_signal_setup_listener(&drm->login->events.attributes_change,
	                         &drm->login_listener,
	                         notify_drm_login_state);
	tw_set_display_destroy_listener(display, &drm->display_destroy,
	                                notify_drm_display_destroy);
	drm->base.render_context_destroy.notify =
		notify_drm_render_context_destroy;

	return &drm->base;

err_boot_gpu:
err_collect_gpus:
err_login:
	drm_backend_destroy(drm);
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
