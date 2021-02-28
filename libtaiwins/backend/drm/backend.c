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
#include <libinput.h>
#include <libudev.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-util.h>
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
#include "taiwins/backend_libinput.h"

extern const struct tw_drm_gpu_impl tw_gpu_gbm_impl;
extern const struct tw_drm_gpu_impl tw_gpu_stream_impl;

static bool
drm_backend_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_drm_display *output;
	struct tw_libinput_device *input;
	struct tw_drm_backend *drm = wl_container_of(backend, drm, base);

	wl_list_for_each(output, &drm->base.outputs, output.device.link) {
		if (output->status.connected && output->status.active)
			tw_drm_display_start(output);
	}
	wl_list_for_each(input, &drm->base.inputs, base.link)
		wl_signal_emit(&drm->base.signals.new_input, &input->base);

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
	wl_signal_emit(&drm->base.signals.stop, &drm->base);
	wl_list_remove(&drm->base.render_context_destroy.link);
	drm->base.ctx = NULL;
}

static inline void
drm_backend_release_gpus(struct tw_drm_backend *drm)
{
	struct tw_drm_gpu *gpu, *tmp_gpu;
	wl_list_for_each_safe(gpu, tmp_gpu, &drm->gpu_list, link)
		tw_drm_backend_remove_gpu(gpu);
}

static void
drm_backend_destroy(struct tw_drm_backend *drm)
{
	struct libinput *libinput = drm->input.libinput;
	wl_signal_emit(&drm->base.signals.destroy, &drm->base);
	if (drm->base.ctx)
		tw_render_context_destroy(drm->base.ctx);
	drm_backend_release_gpus(drm);
	tw_libinput_input_fini(&drm->input);
	libinput_unref(libinput);
	tw_login_destroy(drm->login);
	free(drm);
}

static bool
drm_backend_init_gpu(struct tw_drm_gpu *gpu, struct tw_login_gpu *login_gpu,
                     struct tw_drm_backend *drm)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(drm->display);

	gpu->drm = drm;
	gpu->gpu_fd = login_gpu->fd;
	gpu->devnum = login_gpu->devnum;
	gpu->boot_vga = login_gpu->boot_vga;
	gpu->crtc_mask = 0;
	wl_list_init(&gpu->link);
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
	gpu->impl->free_gpu_device(gpu);
err_handle:
	tw_drm_free_gpu_resources(gpu);
err_resources:
err_features:
	gpu->activated = false;
	return false;
}

void
tw_drm_backend_remove_gpu(struct tw_drm_gpu *gpu)
{
	struct tw_drm_backend *drm = gpu->drm;

	wl_list_remove(&gpu->link);
	wl_event_source_remove(gpu->event);
	tw_drm_free_gpu_resources(gpu);
	gpu->impl->free_gpu_device(gpu);
	tw_login_close(drm->login, gpu->gpu_fd);
	free(gpu);
}

static struct tw_drm_gpu *
drm_gpu_find_create(struct tw_drm_backend *drm, struct tw_login_gpu *login_gpu)
{
	struct tw_drm_gpu *gpu = NULL;
	wl_list_for_each(gpu, &drm->gpu_list, link)
		if (gpu->gpu_fd == login_gpu->fd &&
		    gpu->devnum == login_gpu->devnum)
			return gpu;

	gpu = calloc(1, sizeof(*gpu));
	if (!gpu)
		goto close_gpu;
	if (!drm_backend_init_gpu(gpu, login_gpu, drm)) {
		free(gpu);
		goto close_gpu;
	}
	wl_list_insert(drm->gpu_list.prev, &gpu->link);
	return gpu;
close_gpu:
	tw_login_close(drm->login, login_gpu->fd);
	return NULL;
}

static bool
drm_backend_collect_gpus(struct tw_drm_backend *drm)
{
	int ngpus = 0;
	struct tw_login_gpu login_gpus[16] = {0};
	struct tw_drm_gpu *gpu = NULL;

	wl_list_init(&drm->gpu_list);

	ngpus = tw_login_find_gpus(drm->login, 16, login_gpus);
	if (ngpus <= 0)
		return false;

	for (int i = 0; i < ngpus; i++)
		drm_gpu_find_create(drm, &login_gpus[i]);

	wl_list_for_each(gpu, &drm->gpu_list, link)
		if (gpu->boot_vga && gpu->activated)
			return true;
	//We shall clean up the resources on failing.
	drm_backend_release_gpus(drm);
	return false;
}

static inline struct tw_drm_gpu *
drm_backend_get_boot_gpu(struct tw_drm_backend *drm)
{
	struct tw_drm_gpu *gpu;

	wl_list_for_each(gpu, &drm->gpu_list, link)
		if (gpu->boot_vga)
			return gpu;
	return NULL;
}

/******************************************************************************
 * libinput handlers
 *****************************************************************************/

static int
handle_open_restricted(const char *path, int flags, void *user_data)
{
	struct tw_libinput_input *input = user_data;
	struct tw_drm_backend *drm = wl_container_of(input, drm, input);
	struct tw_login *login = drm->login;

	return tw_login_open(login, path, flags);
}

static void
handle_close_restricted(int fd, void *user_data)
{
	struct tw_libinput_input *input = user_data;
	struct tw_drm_backend *drm = wl_container_of(input, drm, input);
	struct tw_login *login = drm->login;

	tw_login_close(login, fd);
}

static const struct libinput_interface drm_libinput_impl = {
	handle_open_restricted,
	handle_close_restricted,
};

static bool
drm_backend_open_libinput(struct tw_drm_backend *drm)
{
	struct wl_display *dpy = drm->display;
	struct udev *udev = drm->login->udev;
	struct libinput *libinput =
		libinput_udev_create_context(&drm_libinput_impl, &drm->input,
		                             udev);
	if (!libinput)
		return false;
	//TODO getting output device from udev
	if (!tw_libinput_input_init(&drm->input, &drm->base, dpy, libinput,
	                            drm->login->seat, NULL)) {
		libinput_unref(libinput);
		return false;
	}
	return true;
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_drm_login_state(struct wl_listener *listener, void *data)
{
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, login_attribute_change);
	struct tw_login *login = data;
	struct tw_drm_display *dpy;

	//TODO: supposed to dirty all the output and handle planes
	if (login->active) {
		tw_libinput_input_enable(&drm->input);
		wl_list_for_each(dpy, &drm->base.outputs, output.device.link)
			tw_drm_display_continue(dpy);
	} else {
		tw_libinput_input_disable(&drm->input);
		wl_list_for_each(dpy, &drm->base.outputs, output.device.link)
			tw_drm_display_pause(dpy);
	}
}

static void
notify_udev_device_change(struct wl_listener *listener, void *data)
{
	struct tw_drm_gpu *gpu;
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, udev_device_change);
	struct udev_device *dev = data;
	dev_t devnum = udev_device_get_devnum(dev);
	const char *action_name = udev_device_get_action(dev);
	enum tw_drm_device_action action =
		tw_drm_device_action_from_name(action_name);

        if (action == TW_DRM_DEV_UNKNOWN)
		return;
        wl_list_for_each(gpu, &drm->gpu_list, link) {
		if (gpu->devnum == devnum) {
			tw_drm_handle_gpu_event(gpu, action);
			return;
		}
	}
	//We are adding a new GPU on demand.
	if (action == TW_DRM_DEV_ADD) {
		struct tw_login_gpu login_gpu = {0};

		if (!tw_login_gpu_new_from_dev(&login_gpu, dev, drm->login)) {
			tw_logl_level(TW_LOG_WARN, "Failed to add new GPU:%s",
			              udev_device_get_devnode(dev));
			return;
		}
		if (!drm_gpu_find_create(drm, &login_gpu)) {
			tw_logl_level(TW_LOG_WARN, "Faild to add new GPU:%s",
			              udev_device_get_devnode(dev));
			return;
		}
	}
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

WL_EXPORT struct tw_backend *
tw_drm_backend_create(struct wl_display *display)
{
	struct tw_drm_backend *drm = calloc(1, sizeof(*drm));

	if (!drm)
		return NULL;
	tw_backend_init(&drm->base);
	drm->display = display;
	drm->base.impl = &drm_impl;
	wl_list_init(&drm->gpu_list);

	drm->login = tw_login_create(display);
	if (!(drm->login))
		goto err_login;
	if (!drm_backend_open_libinput(drm))
		goto err_libinput;
	if (!drm_backend_collect_gpus(drm))
		goto err_collect_gpus;
	drm->boot_gpu = drm_backend_get_boot_gpu(drm);
	if (!drm->boot_gpu)
		goto err_boot_gpu;

	//add listeners
	tw_signal_setup_listener(&drm->login->signals.attributes_change,
	                         &drm->login_attribute_change,
	                         notify_drm_login_state);
	tw_signal_setup_listener(&drm->login->signals.udev_device,
	                         &drm->udev_device_change,
	                         notify_udev_device_change);
	tw_set_display_destroy_listener(display, &drm->display_destroy,
	                                notify_drm_display_destroy);
	drm->base.render_context_destroy.notify =
		notify_drm_render_context_destroy;

	return &drm->base;

err_boot_gpu:
err_collect_gpus:
err_libinput:
err_login:
	drm_backend_destroy(drm);
	return NULL;
}

WL_EXPORT struct tw_login *
tw_drm_backend_get_login(struct tw_backend *base)
{
	struct tw_drm_backend *drm = NULL;

        assert(base->impl == &drm_impl);
	drm = wl_container_of(base, drm, base);
	return drm->login;
}

WL_EXPORT bool
tw_backend_is_drm(struct tw_backend *backend)
{
	return backend->impl == &drm_impl;
}
