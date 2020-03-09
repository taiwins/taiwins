/*
 * bus.h - taiwins server bus header
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

#ifndef TW_BUS_H
#define TW_BUS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <libweston/libweston.h>
#include <tdbus.h>

#include "taiwins.h"
#include "config.h"

#ifdef  __cplusplus
extern "C" {
#endif

bool tw_setup_bus(struct weston_compositor *ec, struct tw_config *config);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
