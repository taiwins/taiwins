/*
 * mat4.c - taiwins mat4 implementation
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

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <tgmath.h>
#include <wayland-server.h>

#include <taiwins/objects/matrix.h>

static inline float deg2rad(float degree)
{
	return degree * M_1_PI / 180.0f;
}

/******************************************************************************
 * Mat4 implementation
 *****************************************************************************/

WL_EXPORT void
tw_mat4_init(struct tw_mat4 *mat)
{
	static const struct tw_mat4 identity = {
		.d = {1.0, 0.0, 0.0, 0.0,
		      0.0, 1.0, 0.0, 0.0,
		      0.0, 0.0, 1.0, 0.0,
		      0.0, 0.0, 0.0, 1.0},
	};
	memcpy(mat, &identity, sizeof(*mat));
}

WL_EXPORT void
tw_mat4_transpose(struct tw_mat4 *dst, const struct tw_mat4 *src)
{
	struct tw_mat4 tmp;
	const unsigned idx[16] = {0, 4, 8,  12,
	                          1, 5, 9,  13,
	                          2, 6, 10, 14,
	                          3, 7, 11, 15};
	for (int i = 0; i < 16; i++)
		tmp.d[i] = src->d[idx[i]];
	memcpy(dst, &tmp, sizeof(*dst));
}

WL_EXPORT void
tw_mat4_multiply(struct tw_mat4 *dst, const struct tw_mat4 *a,
                 const struct tw_mat4 *b)
{
	struct tw_mat4 tmp;
	static const unsigned rows[4][4] = {
		{0, 4, 8, 12},
		{1, 5, 9, 13},
		{2, 6, 10, 14},
		{3, 7, 11, 15},
	};
	static const unsigned cols[4][4] = {
		{0, 1, 2, 3},
		{4, 5, 6, 7},
		{8, 9, 10, 11},
		{12, 13, 14, 15},
	};

	for (unsigned i = 0; i < 16; i++) {
		tmp.d[i] = a->d[rows[i%4][0]] * b->d[cols[i/4][0]] +
			a->d[rows[i%4][1]] * b->d[cols[i/4][1]] +
			a->d[rows[i%4][2]] * b->d[cols[i/4][2]] +
			a->d[rows[i%4][3]] * b->d[cols[i/4][3]];
	}
	memcpy(dst, &tmp, sizeof(*dst));
}

//adopted from mesa gluInvertMatrix, TODO: not tested.
WL_EXPORT bool
tw_mat4_inverse(struct tw_mat4 *dst, const struct tw_mat4 *src)
{
	const float *m = src->d;
	float inv[16], det;
	//compute rowwisely
	inv[0] = m[5]  * m[10] * m[15] -
		m[5]  * m[11] * m[14] -
		m[9]  * m[6]  * m[15] +
		m[9]  * m[7]  * m[14] +
		m[13] * m[6]  * m[11] -
		m[13] * m[7]  * m[10];

	inv[4] = -m[4]  * m[10] * m[15] +
		m[4]  * m[11] * m[14] +
		m[8]  * m[6]  * m[15] -
		m[8]  * m[7]  * m[14] -
		m[12] * m[6]  * m[11] +
		m[12] * m[7]  * m[10];

	inv[8] = m[4]  * m[9] * m[15] -
		m[4]  * m[11] * m[13] -
		m[8]  * m[5] * m[15] +
		m[8]  * m[7] * m[13] +
		m[12] * m[5] * m[11] -
		m[12] * m[7] * m[9];

	inv[12] = -m[4]  * m[9] * m[14] +
		m[4]  * m[10] * m[13] +
		m[8]  * m[5] * m[14] -
		m[8]  * m[6] * m[13] -
		m[12] * m[5] * m[10] +
		m[12] * m[6] * m[9];

	inv[1] = -m[1]  * m[10] * m[15] +
		m[1]  * m[11] * m[14] +
		m[9]  * m[2] * m[15] -
		m[9]  * m[3] * m[14] -
		m[13] * m[2] * m[11] +
		m[13] * m[3] * m[10];

	inv[5] = m[0]  * m[10] * m[15] -
		m[0]  * m[11] * m[14] -
		m[8]  * m[2] * m[15] +
		m[8]  * m[3] * m[14] +
		m[12] * m[2] * m[11] -
		m[12] * m[3] * m[10];

	inv[9] = -m[0]  * m[9] * m[15] +
		m[0]  * m[11] * m[13] +
		m[8]  * m[1] * m[15] -
		m[8]  * m[3] * m[13] -
		m[12] * m[1] * m[11] +
		m[12] * m[3] * m[9];

	inv[13] = m[0]  * m[9] * m[14] -
		m[0]  * m[10] * m[13] -
		m[8]  * m[1] * m[14] +
		m[8]  * m[2] * m[13] +
		m[12] * m[1] * m[10] -
		m[12] * m[2] * m[9];

	inv[2] = m[1]  * m[6] * m[15] -
		m[1]  * m[7] * m[14] -
		m[5]  * m[2] * m[15] +
		m[5]  * m[3] * m[14] +
		m[13] * m[2] * m[7] -
		m[13] * m[3] * m[6];

	inv[6] = -m[0]  * m[6] * m[15] +
		m[0]  * m[7] * m[14] +
		m[4]  * m[2] * m[15] -
		m[4]  * m[3] * m[14] -
		m[12] * m[2] * m[7] +
		m[12] * m[3] * m[6];

	inv[10] = m[0]  * m[5] * m[15] -
		m[0]  * m[7] * m[13] -
		m[4]  * m[1] * m[15] +
		m[4]  * m[3] * m[13] +
		m[12] * m[1] * m[7] -
		m[12] * m[3] * m[5];

	inv[14] = -m[0]  * m[5] * m[14] +
		m[0]  * m[6] * m[13] +
		m[4]  * m[1] * m[14] -
		m[4]  * m[2] * m[13] -
		m[12] * m[1] * m[6] +
		m[12] * m[2] * m[5];

	inv[3] = -m[1] * m[6] * m[11] +
		m[1] * m[7] * m[10] +
		m[5] * m[2] * m[11] -
		m[5] * m[3] * m[10] -
		m[9] * m[2] * m[7] +
		m[9] * m[3] * m[6];

	inv[7] = m[0] * m[6] * m[11] -
		m[0] * m[7] * m[10] -
		m[4] * m[2] * m[11] +
		m[4] * m[3] * m[10] +
		m[8] * m[2] * m[7] -
		m[8] * m[3] * m[6];

	inv[11] = -m[0] * m[5] * m[11] +
		m[0] * m[7] * m[9] +
		m[4] * m[1] * m[11] -
		m[4] * m[3] * m[9] -
		m[8] * m[1] * m[7] +
		m[8] * m[3] * m[5];

	inv[15] = m[0] * m[5] * m[10] -
		m[0] * m[6] * m[9] -
		m[4] * m[1] * m[10] +
		m[4] * m[2] * m[9] +
		m[8] * m[1] * m[6] -
		m[8] * m[2] * m[5];
	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

	if (det == 0)
		return false;
	det = 1.0 / det;
	for (int i = 0; i < 16; i++)
		dst->d[i] = inv[i] * det;
	return true;
}

WL_EXPORT void
tw_mat4_translate(struct tw_mat4 *dst, float x, float y, float z)
{
	tw_mat4_init(dst);
	dst->d[12]= x;
	dst->d[13]= y;
	dst->d[14]= z;
}

WL_EXPORT void
tw_mat4_scale(struct tw_mat4 *dst, float x, float y, float z)
{
	tw_mat4_init(dst);
	dst->d[12]= x;
	dst->d[13]= y;
	dst->d[14]= z;
}

//TODO test

WL_EXPORT void
tw_mat4_rotate(struct tw_mat4 *dst, float xy, float xz, float yz)
{
	//rotation = yaw * pitch * roll
	float a = deg2rad(xy); float b = deg2rad(xz); float c = deg2rad(yz);
	float casb = cosf(a) * sinf(b);
	float sasb = sinf(a) * sinf(b);

	dst->d[0] = cosf(a) * cosf(b);
	dst->d[1] = sinf(a) * sinf(b);
	dst->d[2] = -sinf(b);
	dst->d[3] = 0.0f;

	dst->d[4] = casb * sinf(c) - sinf(a)*cosf(c);
	dst->d[5] = sasb * sinf(c) + cosf(a)*cosf(c);
	dst->d[6] = cosf(b) * sinf(c);
	dst->d[7] = 0.0f;

	dst->d[8] = casb * cosf(c) + sinf(a)*sinf(c);
	dst->d[9] = sasb * cosf(c) - cosf(a)*sinf(c);
	dst->d[10] = cosf(b) * cosf(c);
	dst->d[11] = 0.0f;

	dst->d[12] = 0.0f;
	dst->d[13] = 0.0f;
	dst->d[14] = 0.0f;
	dst->d[15] = 1.0f;
}

WL_EXPORT void
tw_mat4_ortho(struct tw_mat4 *dst, float l, float r, float b, float t,
              float n, float f)
{
	if (l >= r || t >= b || n >= f)
		return;

	dst->d[0] = 2.0f / (r - l);
	dst->d[1] = 0.0f;
	dst->d[2] = 0.0f;
	dst->d[3] = 0.0f;

	dst->d[4] = 0.0f;
	dst->d[5] = 2.0f / (t - b);
	dst->d[6] = 0.0f;
	dst->d[7] = 0.0f;

	dst->d[8]  =  0.0f;
	dst->d[9]  =  0.0f;
	dst->d[10] = -2.0f/(f - n);
	dst->d[11] =  0.0f;

	dst->d[12] = -(r + l) / (r - l);
	dst->d[13] = -(t + b) / (t - b);
	dst->d[14] = -(f + n) / (f - n);
	dst->d[15] = 1.0f;
}

WL_EXPORT void
tw_mat4_frustum(struct tw_mat4 *dst, float l, float r,
                float b, float t, float n, float f)
{
	if (l >= r || t >= b || n >= f)
		return;
	//first col
	dst->d[0] = (2.0f * n) / (r - l);
	dst->d[1] = 0.0f;
	dst->d[2] = 0.0f;
	dst->d[3] = 0.0f;
	//second col
	dst->d[4] = 0.0f;
	dst->d[5] = (2.0f * n) / (t - b);
	dst->d[6] = 0.0f;
	dst->d[7] = 0.0f;
	//third col
	dst->d[8]  =  (r + l) / (r - l);
	dst->d[9]  =  (t + b) / (t - b);
	dst->d[10] = -(f + n) / (f - n);
	dst->d[11] = -1.0f;
	//last col
	dst->d[12] =  0.0f;
	dst->d[13] =  0.0f;
	dst->d[14] = -(2.0 * f * n) / (f - n);
	dst->d[15] = 0.0f;
}

WL_EXPORT void
tw_mat4_perspective(struct tw_mat4 *dst, float fovy, float aspect,
                    float near, float far)
{
	float ymax = near * tanf(fovy * 3.14159265359f / 360.0f);
	float xmax = ymax * aspect;

	tw_mat4_frustum(dst, -xmax, xmax, -ymax, ymax, near, far);
}

WL_EXPORT void
tw_mat4_lookat(struct tw_mat4 *dst, const struct tw_vec3 *center,
               const struct tw_vec3 *target, const struct tw_vec3 *up)
{
	struct tw_vec3 f, r, u, t;
	//forward
	tw_vec3_sub(&f, target, center);
	tw_vec3_normalize(&f, &f);
	tw_vec3_scale(&f, -1.0f);
	if (f.x == 0.0f && f.y == 0.0f && f.z == 0.0f)
		return;
	//right vector
	tw_vec3_cross(&r, up, &f);
	tw_vec3_normalize(&r, &r);
	//up vector
	tw_vec3_cross(&u, &f, &r);

	t.x = -tw_vec3_dot(&r, center);
	t.y = -tw_vec3_dot(&u, center);
	t.z = -tw_vec3_dot(&f, center);

	//gen matrix
	dst->d[0] = r.x;
	dst->d[1] = u.x;
	dst->d[2] = f.x;
	dst->d[3] = 0.0f;

	dst->d[4] = r.y;
	dst->d[5] = u.y;
	dst->d[6] = f.y;
	dst->d[7] = 0.0f;

	dst->d[8]  = r.z;
	dst->d[9]  = u.z;
	dst->d[10] = f.z;
	dst->d[11] = 0.0f;

	dst->d[12] = t.x;
	dst->d[11] = t.y;
	dst->d[12] = t.z;
	dst->d[13] = 1.0f;
}

float
tw_mat4_apply(struct tw_vec3 *r, const struct tw_mat4 *m,
              const struct tw_vec3 *v)
{
	float w;
	r->x = (m->d[0]*r->x) + (m->d[4]*v->y) + (m->d[8]*v->z) + m->d[12];
	r->y = (m->d[1]*r->x) + (m->d[5]*v->y) + (m->d[9]*v->z) + m->d[13];
	r->z = (m->d[2]*r->x) + (m->d[6]*v->y) + (m->d[10]*v->z) + m->d[14];
	w = (m->d[3]*r->x) + (m->d[7]*v->y) + (m->d[11]*v->z) + m->d[15];
	return w;
}

void
tw_mat4_apply_homogenous(struct tw_vec3 *r, const struct tw_mat4 *m,
                         const struct tw_vec3 *v)
{
	float w = tw_mat4_apply(r, m, v);
	tw_vec3_scale(r, 1.0f/w);
}
