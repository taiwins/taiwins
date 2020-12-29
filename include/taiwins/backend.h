/*
 * backend.h - taiwins server backend header
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

#ifndef TW_BACKEND1_H
#define TW_BACKEND1_H

#include "render_context.h"
#include "output_device.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend;

struct tw_backend_impl {
	bool (*start)(struct tw_backend *backend,
	              struct tw_render_context *ctx);
	const struct tw_egl_options *
	(*gen_egl_params)(struct tw_backend *source);
};

/**
 * @brief an abstract interface all types of backend need to do implement
 */
struct tw_backend {
	const struct tw_backend_impl *impl;
	struct tw_render_context *ctx;

	//we have this events for now as we cannot be include directly into
	//tw_backend. Later, these signals will get throw away and here we call
	//`tw_backend_new_output` or `tw_backend_new_input` directly.
	struct {
		struct wl_signal destroy;
		struct wl_signal new_input;
		struct wl_signal new_output;
		struct wl_signal start;
		struct wl_signal stop; /* emit on render context lost */
	} signals;

	struct wl_list inputs;
	struct wl_list outputs;

	bool started;
	struct wl_listener render_context_destroy;
};

struct tw_backend *
tw_backend_create_auto(struct wl_display *display);

void
tw_backend_init(struct tw_backend *backend);

void
tw_backend_start(struct tw_backend *backend, struct tw_render_context *ctx);

const struct tw_egl_options *
tw_backend_get_egl_params(struct tw_backend *backend);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
