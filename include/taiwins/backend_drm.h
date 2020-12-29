/*
 * backend-drm.h - taiwins server drm backend header
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

#ifndef TW_DRM_BACKEND_H
#define TW_DRM_BACKEND_H

#include <wayland-server.h>
#include <libudev.h>
#include <sys/types.h>

#include "backend.h"
#include "input_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_login;

struct tw_login_impl {
	int (*open)(struct tw_login *login, const char *path, uint32_t flags);
	void (*close)(struct tw_login *login, int fd);
	bool (*switch_vt)(struct tw_login *login, unsigned int vt);
	int (*get_vt)(struct tw_login *login);

	//other methods like sleep, hibernate may need to be supported
};

struct tw_login {
	char seat[32];

	struct udev *udev;
	struct udev_monitor *mon;
	struct wl_event_source *udev_event;
	const struct tw_login_impl *impl;

	/* attributes */
	bool active;

	struct {
		struct wl_signal attributes_change;
		struct wl_signal udev_device;
	} events;

};

struct tw_backend *
tw_drm_backend_create(struct wl_display *display);

struct tw_login *
tw_drm_backend_get_login(struct tw_backend *backend);

int
tw_login_open(struct tw_login *login, const char *path, uint32_t flags);

void
tw_login_close(struct tw_login *login, int fd);

bool
tw_login_switch_vt(struct tw_login *login, unsigned vt);

int
tw_login_get_vt(struct tw_login *login);


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
