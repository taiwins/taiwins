/*
 * renderer.h - taiwins backend renderer header
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

#ifndef TW_RENDERER_H
#define TW_RENDERER_H

#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct gles_procs;
struct tw_renderer {
	struct wlr_renderer base;
	/* additional interfaces */

	struct {
		struct wl_signal pre_output_render;
		struct wl_signal post_ouptut_render;
		struct wl_signal pre_view_render;
		struct wl_signal post_view_render;
	} events;
};

struct wlr_renderer *
tw_renderer_create(struct wlr_egl *egl, EGLenum platform,
                   void *remote_display, EGLint *config_attribs,
                   EGLint visual_id);


#ifdef  __cplusplus
}
#endif

#endif
