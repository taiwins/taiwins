/*
 * matrix.h - taiwins matrix headers
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

#ifndef TW_MATRIX_H
#define TW_MATRIX_H

#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#define TW_EPSILON 1.0e-6

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_mat3 {
	float d[9]; /**< column major */
};

//we will use pixman style signatures

void
tw_mat3_init(struct tw_mat3 *mat);

void
tw_mat3_transpose(struct tw_mat3 *dst, const struct tw_mat3 *src);

void
tw_mat3_multiply(struct tw_mat3 *dst,
                 const struct tw_mat3 *a,
                 const struct tw_mat3 *b);
void
tw_mat3_vec_transform(const struct tw_mat3 *mat, float x, float y,
                      float *rx, float *ry);
void
tw_mat3_box_transform(const struct tw_mat3 *mat,
                      pixman_box32_t *dst, const pixman_box32_t *src);
void
tw_mat3_region_transform(const struct tw_mat3 *mat,
                         pixman_region32_t *dst, pixman_region32_t *src);
void
tw_mat3_scale(struct tw_mat3 *dst, float x, float y);

void
tw_mat3_translate(struct tw_mat3 *mat, float x, float y);
/* rotate ccw */
void
tw_mat3_rotate(struct tw_mat3 *mat, float degree, bool yup);

/**
 * @brief rotate by wl_transform
 *
 * @param yup is true for x-right, y-up coordinates system, OpenGL uses this
 * system, where bottom-left is the origin. yup is false for x-right, y-down
 * coordinates system, wayland coordinates system uses this coordinates, where
 * top-left is the origin.
 */
void
tw_mat3_wl_transform(struct tw_mat3 *dst,
                     enum wl_output_transform transform, bool yup);
/**
 * @breif transform a (0, 0, width, height) by its size
 *
 * the (width, height) here is needed to move the rect back to origin after
 * multiplied by rotation and scaling matrix
 */
void
tw_mat3_transform_rect(struct tw_mat3 *dst, bool yup,
                       enum wl_output_transform transform,
                       uint32_t width, uint32_t height, uint32_t scale);
/**
 * @brief generating matrix mapping coordinates from image space to clip space.
 *
 * @param width is the width of the transformed output.
 * @param height is the height pf the transformed output.
 *
 * The mapping procedure goes as following:
 *
 * (0, 0, width, height) --> (-1, -1, 1, 1)
 * xp = 2*x/width - 1.0
 * yp = 2*y/height - 1.0
 *
 * We take the image space coordiantes(in the wl_transformed output), which the
 * origin is the top-left cornor. We need to map them into clip space, which the
 * origin is the center of monitor. Then we apply the inverse wl_transform of
 * the output to get actual device coordinates.
 *
 * NDC_coord = wl_transform * proj * image_coord
 *
 * Additional transformation can happen between projection and wl_transform, for
 * example an additional scaling can be applyed.
 */
void
tw_mat3_ortho_proj(struct tw_mat3 *dst, uint32_t width, uint32_t height);

bool
tw_mat3_inverse(struct tw_mat3 *dst, const struct tw_mat3 *src);

struct tw_vec3 {
	float x; float y; float z;
};

void
tw_vec3_add(struct tw_vec3 *r, const struct tw_vec3 *a,
            const struct tw_vec3 *b);
void
tw_vec3_sub(struct tw_vec3 *r, const struct tw_vec3 *a,
            const struct tw_vec3 *b);
void
tw_vec3_scale(struct tw_vec3 *r, const float s);

float
tw_vec3_dot(const struct tw_vec3 *a, const struct tw_vec3 *b);

void
tw_vec3_normalize(struct tw_vec3 *dst, const struct tw_vec3 *src);

void
tw_vec3_cross(struct tw_vec3 *dst, const struct tw_vec3 *a,
              const struct tw_vec3 *b);

struct tw_mat4 {
	float d[16]; /**< column major */
};

void
tw_mat4_init(struct tw_mat4 *mat);

void
tw_mat4_transpose(struct tw_mat4 *dst, const struct tw_mat4 *src);

bool
tw_mat4_inverse(struct tw_mat4 *dst, const struct tw_mat4 *src);

void
tw_mat4_multiply(struct tw_mat4 *dst, const struct tw_mat4 *a,
                 const struct tw_mat4 *b);
float
tw_mat4_apply(struct tw_vec3 *r, const struct tw_mat4 *m,
              const struct tw_vec3 *v);
void
tw_mat4_apply_homogenous(struct tw_vec3 *r, const struct tw_mat4 *m,
                         const struct tw_vec3 *v);
void
tw_mat4_translate(struct tw_mat4 *dst, float x, float y, float z);

void
tw_mat4_scale(struct tw_mat4 *dst, float x, float y, float z);

//TODO
void
tw_mat4_rotate(struct tw_mat4 *dst, float xy_degree, float xz_degree,
               float yz_degree);
//TODO test
void
tw_mat4_lookat(struct tw_mat4 *dst, const struct tw_vec3 *center,
               const struct tw_vec3 *target, const struct tw_vec3 *up);
//TODO test
void
tw_mat4_ortho(struct tw_mat4 *dst, float left, float right,
              float bottom, float top, float near, float far);
//TODO test
void
tw_mat4_frustum(struct tw_mat4 *dst, float left, float right,
                float bottom, float top, float near, float far);
//TODO test
void
tw_mat4_perspective(struct tw_mat4 *dst, float fovy, float aspect,
                    float near, float far);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
