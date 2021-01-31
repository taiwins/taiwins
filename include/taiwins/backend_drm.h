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
#include "login.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend *
tw_drm_backend_create(struct wl_display *display);

struct tw_login *
tw_drm_backend_get_login(struct tw_backend *backend);


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
