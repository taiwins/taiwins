/*
 * backend.c - taiwins server backend implementation
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
#include "options.h"

#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/egl.h>

#include <taiwins/backend.h>
#include <taiwins/backend_headless.h>
#include <taiwins/backend_wayland.h>
#if _TW_HAS_X11_BACKEND
#include <taiwins/backend_x11.h>
#endif

void
tw_backend_init(struct tw_backend *backend)
{
	backend->started = false;
	wl_signal_init(&backend->events.new_input);
	wl_signal_init(&backend->events.new_output);
	wl_signal_init(&backend->events.destroy);
	wl_signal_init(&backend->events.start);
	wl_signal_init(&backend->events.stop);

	wl_list_init(&backend->inputs);
	wl_list_init(&backend->outputs);

	wl_list_init(&backend->render_context_destroy.link);
}

const struct tw_egl_options *
tw_backend_get_egl_params(struct tw_backend *backend)
{
	return backend->impl->gen_egl_params(backend);
}

void
tw_backend_start(struct tw_backend *backend, struct tw_render_context *ctx)
{
	assert(backend->impl->start);

	backend->ctx = ctx;
	backend->impl->start(backend, ctx);
	backend->started = true;
	wl_signal_add(&ctx->events.destroy, &backend->render_context_destroy);
	wl_signal_emit(&backend->events.start, backend);
}


struct tw_backend *
tw_backend_create_auto(struct wl_display *display)
{
	struct tw_backend *backend = NULL;
	if (getenv("WAYLAND_DISPLAY") != NULL)
		backend = tw_wayland_backend_create(display,
		                                    getenv("WAYLAND_DISPLAy"));
#if _TW_HAS_X11_BACKEND
	else if(getenv("DISPLAY") != NULL)
		backend = tw_x11_backend_create(display,
		                                getenv("DISPLAY"));
#endif
	else
		assert(0);
	return backend;
}
