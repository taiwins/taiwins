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

#include <stdlib.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wayland-server.h>
#include <wayland-client.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/backend/backend.h>

#include "internal.h"

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


static const struct tw_backend_impl wl_impl = {

};


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
        wl_list_init(&wl->outputs);

        if (!wl->remote_display) {
	        tw_logl_level(TW_LOG_ERRO, "failed to connect to wl_display");
	        goto err_remote;
        }
        wl->registry = wl_display_get_registry(wl->remote_display);
        if (!wl->registry) {
	        tw_logl_level(TW_LOG_ERRO, "failed to obtain remote registry");
	        goto err_registry;
        }

        tw_backend_init(&wl->base);

        wl->base.render_context_destroy.notify =
	        notify_wl_render_context_destroy;

        tw_set_display_destroy_listener(display, &wl->display_destroy,
                                        notify_wl_display_destroy);

        //in both libweston and wlroots, the backend initialize a renderer, it
        //make sense since only the backend knows about the correct renderer
        //options,
        return &wl->base;
err_registry:
        wl_display_disconnect(wl->remote_display);
err_remote:
        free(wl);
        return NULL;
}
