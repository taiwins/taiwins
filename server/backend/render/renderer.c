/*
 * renderer.c - taiwins backend renderer functions
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

#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>

#include "ctypes/helpers.h"
#include "objects/dmabuf.h"
#include "objects/logger.h"
#include "objects/surface.h"
#include "pixman.h"
#include "shaders.h"
#include "renderer.h"
#include "wlr/render/drm_format_set.h"

static const EGLint gles3_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
	EGL_NONE,
};

static const EGLint gles2_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE,
};

/******************************************************************************
 * interface
 *****************************************************************************/

static const struct wlr_renderer_impl renderer_impl = {

};

static inline bool
check_externsion(const char *exts, const char *ext)
{
	//you can use strstr, or strtok_r to query the strings
	return strstr(exts, ext) != NULL;
}


/**
 * for completing the renderer, we would setup the callbacks, and most
 * importantly, creating the shaders and vao, vbos
 */
static void
init_renderer_interface(struct tw_renderer *renderer, struct wlr_egl *egl)
{
	struct tw_quad_color_shader color_shader;
	struct tw_quad_tex_shader tex_shader, blur_shader;

	wlr_renderer_init(&renderer->base, &renderer_impl);
	wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);

	//check extensions.
	//in our case we may need different extensions to get EGLImage, for now
	//lets skip.

	tw_quad_color_shader_init(&color_shader);
	tw_quad_tex_blend_shader_init(&tex_shader);
	tw_quad_tex_blur_shader_init(&blur_shader);

	tw_quad_color_shader_fini(&color_shader);
	tw_quad_tex_blend_shader_fini(&tex_shader);
	tw_quad_tex_blur_shader_fini(&blur_shader);
}

struct wlr_renderer *
tw_renderer_create(struct wlr_egl *egl, EGLenum platform,
                   void *remote_display, EGLint *config_attribs,
                   EGLint visual_id)
{
	bool init = false;
	struct tw_renderer *renderer = calloc(1, sizeof(struct tw_renderer));
	if (!renderer)
		return NULL;

	init = wlr_egl_init(egl, platform, remote_display,
	                    gles3_config_attribs, visual_id);
	if (!init)
		init = wlr_egl_init(egl, platform, remote_display,
		                    gles2_config_attribs, visual_id);
	if (!init) {
		//the initialization code does plenty of work in
		//eglInitialize, eglQueryString, bind function pointers
		//I am not sure if you can call it twice though
		return NULL;
	}
	init_renderer_interface(renderer, egl);

	wl_signal_init(&renderer->events.pre_output_render);
	wl_signal_init(&renderer->events.post_ouptut_render);
	wl_signal_init(&renderer->events.pre_view_render);
	wl_signal_init(&renderer->events.post_view_render);

	return &renderer->base;
}
