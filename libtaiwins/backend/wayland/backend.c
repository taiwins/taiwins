/*
 * wayland.c - taiwins server wayland backend implementation
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
#include <stdlib.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-client.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>
#include <taiwins/backend/backend.h>
#include <taiwins/render_context.h>
#include <wayland-util.h>

#include "internal.h"

static int
wl_backend_dispatch_events(int fd, uint32_t mask, void *data)
{
	int count = 0;
	struct tw_wl_backend *wl = data;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		if (mask & WL_EVENT_ERROR) {
			tw_logl_level(TW_LOG_ERRO,
			              "Failed to read from remote display");
		}
		wl_display_terminate(wl->server_display);
		return 0;
	}
	if (mask & WL_EVENT_READABLE)
		count = wl_display_dispatch(wl->remote_display);
	if (mask & WL_EVENT_WRITABLE)
		wl_display_flush(wl->remote_display);
	if (mask == 0) {
		count = wl_display_dispatch_pending(wl->remote_display);
		wl_display_flush(wl->remote_display);
	}

	if (count < 0) {
		tw_logl_level(TW_LOG_ERRO,
		              "Failed to dispatch remote display");
		wl_display_terminate(wl->server_display);
		return 0;
	}
	return count;
}

static void
wl_backend_stop(struct tw_wl_backend *wl)
{
	if (!wl->base.ctx)
		return;
	wl_signal_emit(&wl->base.events.stop, &wl->base);
	wl_list_remove(&wl->base.render_context_destroy.link);
	wl->base.ctx = NULL;
}

static void
wl_backend_destroy(struct tw_wl_backend *wl)
{
	wl_signal_emit(&wl->base.events.destroy, &wl->base);

        wl_list_remove(&wl->display_destroy.link);

	if (wl->base.ctx)
		tw_render_context_destroy(wl->base.ctx);

	free(wl);
}

static void
notify_wl_render_context_destroy(struct wl_listener *listener, void *data)
{
	struct tw_wl_backend *wl =
		wl_container_of(listener, wl,
		                base.render_context_destroy);
	wl_backend_stop(wl);
}

static void
notify_wl_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_wl_backend *wl =
		wl_container_of(listener, wl, display_destroy);

	wl_backend_destroy(wl);
}

/******************************************************************************
 * backend implemenentation
 *****************************************************************************/

static const struct tw_egl_options *
wl_gen_egl_params(struct tw_backend *backend)
{
	static struct tw_egl_options egl_opts = {0};
	struct tw_wl_backend *wl = wl_container_of(backend, wl, base);

	static const EGLint egl_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};

	egl_opts.platform = EGL_PLATFORM_WAYLAND_KHR;
	egl_opts.native_display = wl->remote_display;
	egl_opts.visual_id = WL_SHM_FORMAT_ARGB8888;
	egl_opts.context_attribs = (EGLint *)egl_config_attribs;

	return &egl_opts;
}

static bool
wl_start_backend(struct tw_backend *backend, struct tw_render_context *ctx)
{
	struct tw_wl_output *output;
	struct tw_wl_seat *seat;
	struct tw_wl_backend *wl = wl_container_of(backend, wl, base);

	wl_display_dispatch_pending(wl->remote_display);

	wl_list_for_each(output, &wl->base.outputs, output.device.link)
		tw_wl_output_start(output);

	wl_list_for_each(seat, &wl->seats, link)
		tw_wl_seat_start(seat);
	//I think the seats also does not work

	return true;
}

static const struct tw_backend_impl wl_impl = {
	.start = wl_start_backend,
	.gen_egl_params = wl_gen_egl_params,
};

/******************************************************************************
 * initializer
 *****************************************************************************/

static bool
wl_backend_set_events(struct tw_wl_backend *wl)
{
	struct wl_event_loop *loop =
		wl_display_get_event_loop(wl->server_display);
	int fd = wl_display_get_fd(wl->remote_display);
	int events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;

	wl->event_src = wl_event_loop_add_fd(loop, fd, events,
	                                     wl_backend_dispatch_events, wl);
	if (!wl->event_src) {
		tw_logl_level(TW_LOG_ERRO, "Failed to create event source");
		return false;
	}
	wl_event_source_check(wl->event_src);
	return true;
}

struct tw_backend *
tw_wayland_backend_create(struct wl_display *display, const char *remote)
{
	struct tw_wl_backend *wl = calloc(1, sizeof(*wl));

        if (!wl)
		return false;
        wl->remote_display = wl_display_connect(remote);
        wl->server_display = display;
        wl->base.impl = &wl_impl;
        wl_list_init(&wl->seats);

        if (!wl->remote_display) {
	        tw_logl_level(TW_LOG_ERRO, "failed to connect to wl_display");
	        goto err_remote;
        }
        wl->registry = wl_display_get_registry(wl->remote_display);
        if (!wl->registry) {
	        tw_logl_level(TW_LOG_ERRO, "failed to obtain remote registry");
	        goto err_registry;
        }
        tw_wl_bind_wl_registry(wl);
        wl_display_roundtrip(wl->remote_display);
        if (!wl->globals.compositor || !wl->globals.wm_base)
	        goto err_registry;
        if (!wl_backend_set_events(wl))
	        goto err_events;

        tw_backend_init(&wl->base);
        wl->base.render_context_destroy.notify =
	        notify_wl_render_context_destroy;

        tw_set_display_destroy_listener(display, &wl->display_destroy,
                                        notify_wl_display_destroy);

        return &wl->base;
err_events:
err_registry:
        wl_display_disconnect(wl->remote_display);
err_remote:
        free(wl);
        return NULL;
}
