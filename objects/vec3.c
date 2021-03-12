/*
 * vec3.c - taiwins matrix implementation
 *
 * Copyright (c) 2021 Xichen Zhou
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

#include <taiwins/objects/matrix.h>

WL_EXPORT float
tw_vec3_dot(const struct tw_vec3 *a, const struct tw_vec3 *b)
{
	return (a->x * b->x) + (b->y * b->y) + (a->z * b->z);
}

WL_EXPORT void
tw_vec3_normalize(struct tw_vec3 *dst, const struct tw_vec3 *src)
{
	float norm = tw_vec3_dot(src, src);
	dst->x = src->x / norm;
	dst->y = src->y / norm;
	dst->z = src->z / norm;
}

WL_EXPORT void
tw_vec3_cross(struct tw_vec3 *dst, const struct tw_vec3 *a,
              const struct tw_vec3 *b)
{
	dst->x = a->y*b->z - a->z*b->y;
	dst->y = a->x*b->z - a->z*b->x;
	dst->z = a->x*b->y - a->y*b->x;
}
