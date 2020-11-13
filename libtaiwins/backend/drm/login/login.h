/*
 * login.h - taiwins login service header
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

#ifndef TW_LOGIN_H
#define TW_LOGIN_H

#include <stdlib.h>
#include <string.h>
#include <libudev.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <taiwins/backend_drm.h>

struct tw_login_gpu {
	int fd, sysnum;
	bool boot_vga;
};

bool
tw_login_init(struct tw_login *login, struct wl_display *display,
              const struct tw_login_impl *impl);

void
tw_login_fini(struct tw_login *login);

struct tw_login *
tw_login_create(struct wl_display *display);

void
tw_login_destroy(struct tw_login *login);

void
tw_login_set_active(struct tw_login *login, bool active);

int
tw_login_find_gpus(struct tw_login *login, struct tw_login_gpu *gpus);

int
tw_login_find_primary_gpu(struct tw_login *login);

#endif /* EOF */
