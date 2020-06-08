/*
 * dmabuf.h - taiwins dmabuf interface
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


#ifndef TW_DMABUF_H
#define TW_DMABUF_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_DMA_MAX_PLANES 4

struct tw_dmabuf_attributes {
	int32_t width, height;
	uint32_t format, flags;

	int n_planes;
	int fds[TW_DMA_MAX_PLANES];
	uint32_t strides[TW_DMA_MAX_PLANES];
	uint32_t offsets[TW_DMA_MAX_PLANES];
	uint64_t modifier;
	bool modifier_used;
};

struct tw_linux_dmabuf {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_listener destroy_listener;
        /**
         * the protocol interface need a backend to does the actual IO work, we
         * have callbacks here to have this dmabuf interface indepedent from
         * actual implementation.
         *
         * The callbacks shall not allocated any data, if formats is NULL, it
         * would just return the number of formats available.
	 */
	struct {
		void (*format_request)(struct tw_linux_dmabuf *dmabuf,
		                       void *callback, int *formats,
		                       size_t *nformats);
		void (*modifiers_request)(struct tw_linux_dmabuf *dmabuf,
		                          void *callback, int format,
		                          uint64_t *modifiers,
		                          size_t *nmodifiers);
		void *callback;
	} format_request;
	/* same as format_request */
	struct {
		bool (*import_buffer)(struct tw_dmabuf_attributes *attrs,
		                      void *callback);
		void *callback;
	} import_buffer;
};

/**
 * @brief a dma param/buffer object
 *
 * param resource is created at dmabuf.create_params. Mutiple planes can be
 * added to this paramter before dmabuf_param.create or
 * dmabuf_param.create_immed is called. After that param_resource is gone and
 * this tw_dmabuf_buffer represents a wl_buffer.
 */
struct tw_dmabuf_buffer {
	struct wl_resource *param_resource;
	struct wl_resource *dma_resource;
	struct wl_resource *buffer_resource;

	struct tw_dmabuf_attributes attributes;
};

struct tw_linux_dmabuf *
tw_dmabuf_create_global(struct wl_display *display);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
