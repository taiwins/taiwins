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

#include "options.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libudev.h>
#include <sys/types.h>
#include <wayland-server-core.h>

#include <taiwins/backend.h>
#include <taiwins/backend_drm.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/render_context.h>

#include "login/login.h"

struct tw_drm_backend {
	struct tw_backend base;
	struct tw_login *login;

	struct wl_listener display_destroy;
};

static void
drm_backend_destroy(struct tw_drm_backend *drm)
{
	wl_signal_emit(&drm->base.events.destroy, &drm->base);
	if (drm->base.ctx)
		tw_render_context_destroy(drm->base.ctx);
#if _TW_HAS_SYSTEMD || _TW_HAS_ELOGIND
	tw_login_destroy_logind(drm->login);
#else
	tw_login_destroy_direct(drm->login);
#endif
	free(drm);
}

static void
notify_drm_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_drm_backend *drm =
		wl_container_of(listener, drm, display_destroy);
	wl_list_remove(&listener->link);
	drm_backend_destroy(drm);
}

static const struct tw_backend_impl drm_impl = {

};

struct tw_backend *
tw_drm_backend_create(struct wl_display *display)
{
	struct tw_drm_backend *drm = calloc(1, sizeof(*drm));

	if (!drm)
		return NULL;
	tw_backend_init(&drm->base);
	drm->base.impl = &drm_impl;

#if _TW_HAS_SYSTEMD || _TW_HAS_ELOGIND
	drm->login = tw_login_create_logind(display);
#else
	drm->login = tw_login_create_direct(display);
#endif
	if (!(drm->login))
		goto err_login;

	tw_set_display_destroy_listener(display, &drm->display_destroy,
	                                notify_drm_display_destroy);

	return &drm->base;


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
