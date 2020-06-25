/*
 * plane.c - taiwins plane implementation
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

#include "plane.h"
#include "pixman.h"
#include <wayland-util.h>

void
tw_plane_init(struct tw_plane *plane)
{
	wl_list_init(&plane->link);
	pixman_region32_init(&plane->clip);
	pixman_region32_init(&plane->damage);
}

void
tw_plane_fini(struct tw_plane *plane)
{
	wl_list_remove(&plane->link);
	pixman_region32_fini(&plane->clip);
	pixman_region32_fini(&plane->damage);
}
