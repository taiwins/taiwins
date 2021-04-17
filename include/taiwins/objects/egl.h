/*
 * egl.h - taiwins EGL renderer interface
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

#ifndef TW_EGL_H
#define TW_EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <assert.h>
#include <string.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "dmabuf.h"
#include "drm_formats.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_dmabuf_attributes;

struct tw_egl_options {
	/** platform like EGL_PLATFORM_GBM_KHR */
	EGLenum platform;
	/** native display type like a wl_display from wayland */
	void *native_display;
	/** visual id represents the format the platform supports */
	EGLint visual_id;

	const EGLint *context_attribs;
	const EGLint *platform_attribs;
};

struct tw_egl {
	struct wl_display *wl_display;

	EGLContext context;
	EGLDisplay display;
	EGLenum platform;
	EGLConfig config;
	bool query_buffer_age, image_base_khr;
	bool import_dmabuf, import_dmabuf_modifiers;
	unsigned int internal_format;
	struct tw_drm_formats drm_formats;
};


bool
tw_egl_init(struct tw_egl *egl, const struct tw_egl_options *opts);

void
tw_egl_fini(struct tw_egl *egl);

bool
tw_egl_check_gl_ext(struct tw_egl *egl, const char *ext);

bool
tw_egl_check_egl_ext(struct tw_egl *egl, const char *ext);

bool
tw_egl_make_current(struct tw_egl *egl, EGLSurface surface);

bool
tw_egl_unset_current(struct tw_egl *egl);

int
tw_egl_buffer_age(struct tw_egl *egl, EGLSurface surface);

bool
tw_egl_bind_wl_display(struct tw_egl *egl, struct wl_display *display);

bool
tw_egl_destroy_image(struct tw_egl *egl, EGLImageKHR image);

bool
tw_egl_query_wl_buffer(struct tw_egl *egl, struct wl_resource *buffer,
                       EGLint attribute, EGLint *value);
EGLSurface
tw_egl_create_window_surface(struct tw_egl *egl, void *native_surface,
                             EGLint visual_id, EGLint const *attrib_list);
EGLImageKHR
tw_egl_import_wl_drm_image(struct tw_egl *egl, struct wl_resource *data,
                           EGLint *fmt, int *width, int *height,
                           bool *y_inverted);
EGLImageKHR
tw_egl_import_dmabuf_image(struct tw_egl *egl,
                           struct tw_dmabuf_attributes *attrs, bool *external);
bool
tw_egl_image_export_dmabuf(struct tw_egl *egl, EGLImage image,
                           int width, int height, uint32_t flags,
                           struct tw_dmabuf_attributes *attrs);
void
tw_egl_impl_linux_dmabuf(struct tw_egl *egl, struct tw_linux_dmabuf *dma);

/** EGL Stream APIs, not guarantee to exit */
EGLDeviceEXT
tw_egl_device_from_path(const char *path);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
