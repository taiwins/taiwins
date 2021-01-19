/*
 * dmabuf.c - taiwins dmabuf interface
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

#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <drm_fourcc.h>
#include <wayland-server.h>
#include <wayland-linux-dmabuf-server-protocol.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/dmabuf.h>

#define DMA_BUF_VERSION 3

static const struct zwp_linux_buffer_params_v1_interface buffer_params_impl;

static struct tw_linux_dmabuf s_tw_linux_dmabuf = {0};

static void tw_dmabuf_buffer_destroy(struct tw_dmabuf_buffer *buffer);

static const struct  wl_buffer_interface  wl_buffer_impl = {
	.destroy = tw_resource_destroy_common,
};

static void
destroy_wl_buffer(struct wl_resource *resource)
{
	struct tw_dmabuf_buffer *buffer =
		tw_dmabuf_buffer_from_resource(resource);
	tw_dmabuf_buffer_destroy(buffer);
}

WL_EXPORT bool
tw_is_wl_buffer_dmabuf(struct wl_resource *resource)
{
	return wl_resource_instance_of(resource, &wl_buffer_interface,
	                               &wl_buffer_impl) != 0;
}

WL_EXPORT struct tw_dmabuf_buffer *
tw_dmabuf_buffer_from_resource(struct wl_resource *resource)
{
	struct tw_dmabuf_buffer *buffer;
	assert(wl_resource_instance_of(resource, &wl_buffer_interface,
	                               &wl_buffer_impl));
	buffer = wl_resource_get_user_data(resource);
	return buffer;
}

/******************************************************************************
 * tw_dmabuf_buffer implementation
 *****************************************************************************/

static struct tw_dmabuf_buffer *
buffer_params_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_linux_buffer_params_v1_interface,
	                               &buffer_params_impl));
	return wl_resource_get_user_data(resource);
}

static struct tw_dmabuf_buffer *
buffer_params_get_params(struct wl_resource *resource)
{
	//get buffer that is not yet converted to wl_buffer
	struct tw_dmabuf_buffer *buffer =
		buffer_params_from_resource(resource);
	if (!buffer)
		return NULL;
	if (!buffer->param_resource || buffer->buffer_resource ||
	    resource != buffer->param_resource)
		return NULL;
	return buffer;
}

static void
tw_dmabuf_attributes_init(struct tw_dmabuf_attributes *attrs)
{
	for (int i = 0; i < TW_DMA_MAX_PLANES; i++) {
		attrs->fds[i] = -1;
	}
	attrs->n_planes = 0;
	attrs->height = -1;
	attrs->width = -1;
	attrs->modifier_used = false;
}

static void
tw_dmabuf_buffer_destroy(struct tw_dmabuf_buffer *buffer)
{
	for (int i = 0; i < buffer->attributes.n_planes; i++) {
		if (buffer->attributes.fds[i] != -1) {
			close(buffer->attributes.fds[i]);
		}
	}
	tw_dmabuf_attributes_init(&buffer->attributes);
	free(buffer);
}

static void
buffer_params_add(struct wl_client *client,
                  struct wl_resource *resource,
                  int32_t fd, uint32_t plane_idx,
                  uint32_t offset, uint32_t stride,
                  uint32_t modifier_hi, uint32_t modifier_lo)
{
	//this would also need to check if buffer has params or wl_buffer
	uint64_t modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;
	struct tw_dmabuf_buffer *buffer =
		buffer_params_get_params(resource);

	if (!buffer) {
		wl_resource_post_error(
			resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
			"params already used to create a wl_buffer");
		goto out;
	}
	if (plane_idx >= TW_DMA_MAX_PLANES) {
		wl_resource_post_error(
			resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
			"plane index %u too big", plane_idx);
		goto out;
	}
	if (buffer->attributes.fds[plane_idx] != -1) {
		wl_resource_post_error(
			resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
			"plane index %u already used by fd %d",
			plane_idx, buffer->attributes.fds[plane_idx]);
		goto out;
	}
	if (buffer->attributes.modifier_used && modifier !=
	    buffer->attributes.modifier) {
		wl_resource_post_error(
			resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
			"modifier already set as %lu",
			buffer->attributes.modifier);
		goto out;
	}
	buffer->attributes.modifier = modifier;
	buffer->attributes.modifier_used = true;
	buffer->attributes.fds[plane_idx] = fd;
	buffer->attributes.offsets[plane_idx] = offset;
	buffer->attributes.strides[plane_idx] = stride;
	buffer->attributes.n_planes++;
	return;
out:
	close(fd);
}

static inline bool
test_import_buffer(struct tw_linux_dmabuf *dma,
                   struct tw_dmabuf_attributes *attributes)
{
	if (dma->impl && dma->impl->test_import &&
	    dma->impl->test_import(attributes, dma->impl_userdata))
		return true;
	else
		return false;
}

static void
buffer_params_create_common(struct wl_client *client,
                            struct wl_resource *param_resource,
                            uint32_t id, int32_t width, int32_t height,
                            uint32_t format, uint32_t flags)
{
	struct tw_linux_dmabuf *dma;
	struct tw_dmabuf_buffer *buffer =
		buffer_params_get_params(param_resource);
	if (!buffer) {
		wl_resource_post_error(
			param_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
			"params already used to create a wl_buffer");
		return;
	}
	//now we reset the param resource
	buffer->param_resource = NULL;
	wl_resource_set_user_data(param_resource, NULL);
	dma = wl_resource_get_user_data(buffer->dma_resource);

	if (!buffer->attributes.n_planes) {
		wl_resource_post_error(param_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
			"no dmabuf has been added to the params");
		goto err_out;
	}
	// Check for holes in the dmabufs set (e.g. [0, 1, 3])
	for (int i = 0; i < buffer->attributes.n_planes; i++) {
		if (buffer->attributes.fds[i] == -1) {
			wl_resource_post_error(param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
				"no dmabuf has been added for plane %i", i);
			goto err_out;
		}
	}
	if (width < 1 || height < 1) {
		wl_resource_post_error(param_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
			"invalid width %d or height %d", width, height);
		goto err_out;
	}
	buffer->attributes.width = width;
	buffer->attributes.height = height;
	buffer->attributes.format = format;
	buffer->attributes.flags = flags;

	for (int i = 0; i < buffer->attributes.n_planes; i++) {
		off_t size;

		if ((uint64_t)buffer->attributes.offsets[i] +
		    buffer->attributes.strides[i] > UINT32_MAX) {
			wl_resource_post_error(
				param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"size overflow for plane %d", i);
			goto err_out;
		}
		if (i == 0 &&
		   (uint64_t) buffer->attributes.offsets[i] +
		   (uint64_t) buffer->attributes.strides[i] * height >
		    UINT32_MAX) {
			wl_resource_post_error(
				param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"size overflow for plane %d", i);
			goto err_out;
		}
		size = lseek(buffer->attributes.fds[i], 0, SEEK_END);
		if (size == -1)
			continue;

		if (buffer->attributes.offsets[i] > size) {
			wl_resource_post_error(
				param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid offset %i for plane %i",
				buffer->attributes.offsets[i], i);
			goto err_out;
		}
		if (buffer->attributes.offsets[i] +
		    buffer->attributes.strides[i] > size) {
			wl_resource_post_error(
				param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid stride %i for plane %i",
				buffer->attributes.strides[i], i);
			goto err_out;
		}
		//Only valid for first plane as other planes might be
		//sub-sampled according to fourcc format
		if (i == 0 && buffer->attributes.offsets[i] +
				buffer->attributes.strides[i]*height > size) {
			wl_resource_post_error(
				param_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid buffer stride or height for plane %d",
				i);
			goto err_out;
		}
	}
	//TODO: now only y_inverted flags are supported
	if (buffer->attributes.flags &
	    ~ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT) {
		wl_resource_post_error(
			param_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
			"unknown dmabuf flags %d", buffer->attributes.flags);
		goto err_out;
	}
	//now the dma buffer need to
	if (!test_import_buffer(dma, &buffer->attributes))
		goto err_failed;

	//at this point we will create a wl_buffer
	buffer->buffer_resource = wl_resource_create(client,
	                                             &wl_buffer_interface,
	                                             1, id);
	if (!buffer->buffer_resource) {
		wl_resource_post_no_memory(param_resource);
		goto err_buffer;
	}
	wl_resource_set_implementation(buffer->buffer_resource,
	                               &wl_buffer_impl, buffer,
	                               destroy_wl_buffer);
	return;
err_buffer:
err_failed:
	if (id == 0)
		zwp_linux_buffer_params_v1_send_failed(param_resource);
	else
		//since the behavior is left implementation defined by the
		//protocol in case of create_immed failure due to an unknown
		//cause, we choose to treat it as a fatal error and immediately
		//kill the client instead of creating an invalid handle and
		//waiting for it to be used.
		wl_resource_post_error(
			param_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
			"importing the dmabuf failed");
err_out:
	tw_dmabuf_buffer_destroy(buffer);
}

static void
buffer_params_create(struct wl_client *client,
                     struct wl_resource *resource,
                     int32_t width, int32_t height,
                     uint32_t format, uint32_t flags)
{
	buffer_params_create_common(client, resource, 0, width, height,
	                            format, flags);
}

static void
buffer_params_create_immed(struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t buffer_id,
                           int32_t width, int32_t height,
                           uint32_t format, uint32_t flags)
{
	buffer_params_create_common(client, resource, buffer_id, width, height,
	                            format, flags);
}

static void
buffer_params_destroy(struct wl_resource *resource)
{
	struct tw_dmabuf_buffer *buffer =
		buffer_params_from_resource(resource);
	free(buffer);
}

static const struct zwp_linux_buffer_params_v1_interface buffer_params_impl = {
	.destroy = tw_resource_destroy_common,
	.add = buffer_params_add,
	.create = buffer_params_create,
	.create_immed = buffer_params_create_immed,
};

/******************************************************************************
 * linux_dmabuf implementation
 *****************************************************************************/

static void
linux_dmabuf_create_params(struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t params_id)
{
	struct tw_dmabuf_buffer *buffer =
		calloc(1, sizeof(struct tw_dmabuf_buffer));
	uint32_t version = wl_resource_get_version(resource);
	if (!buffer)
		goto err_buffer;
	buffer->param_resource =
		wl_resource_create(client,
		                   &zwp_linux_buffer_params_v1_interface,
		                   version, params_id);
	buffer->dma_resource = resource;
	if (!buffer->param_resource)
		goto err_resource;
	wl_resource_set_implementation(buffer->param_resource,
	                               &buffer_params_impl,
	                               buffer, buffer_params_destroy);
	tw_dmabuf_attributes_init(&buffer->attributes);
	return;
err_resource:
	free(buffer);
err_buffer:
	wl_resource_post_no_memory(resource);
}

static const struct zwp_linux_dmabuf_v1_interface dmabuf_v1_impl = {
	.destroy = tw_resource_destroy_common,
	.create_params = linux_dmabuf_create_params,
};

static void
dmabuf_destroy_resource(struct wl_resource *resource)
{
}

static bool
dmabuf_send_formats(struct tw_linux_dmabuf *dma,
                    struct wl_resource *resource, uint32_t v)
{
	size_t n_formats = 0, n_modifiers = 0;
	uint32_t modifier_lo, modifier_hi;

        if (!dma->impl || !dma->impl->format_request ||
	    !dma->impl->modifiers_request)
		return NULL;

	dma->impl->format_request(dma, dma->impl_userdata,
	                          NULL, &n_formats);
	if (!n_formats)
		return false;
	assert(n_formats > 0);
	int formats[n_formats];
	dma->impl->format_request(dma, dma->impl_userdata,
	                          formats, &n_formats);
	//send DRM_FORMAT_MOD_INVALID token when no modifiers are supported for
	//this format
        for (size_t i = 0; i < n_formats; i++) {
	        bool no_modifiers = false;
		dma->impl->modifiers_request(dma, dma->impl_userdata,
		                             formats[i], NULL, &n_modifiers);
		if (!n_modifiers) {
			no_modifiers = true;
			n_modifiers = 1;
		}
		assert(n_modifiers > 0);
		uint64_t modifiers[n_formats];
		dma->impl->modifiers_request(dma, dma->impl_userdata,
		                             formats[i], modifiers,
		                             &n_modifiers);
		if (!n_modifiers) {
			n_modifiers = 1;
			modifiers[0]= DRM_FORMAT_MOD_INVALID;
		}
		for (unsigned j = 0; j < n_modifiers; j++) {
			 if (v >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
				 modifier_lo = modifiers[j] & 0xFFFFFFFF;
				 modifier_hi = modifiers[j] >> 32;
				 zwp_linux_dmabuf_v1_send_modifier(
					 resource, formats[i],
					 modifier_hi, modifier_lo);
			 } else if (modifiers[j] == DRM_FORMAT_MOD_LINEAR ||
			            no_modifiers) {
				 zwp_linux_dmabuf_v1_send_format(resource,
				                                 formats[i]);
			 }
		}
	}
        return true;
}

static void
bind_dmabuf(struct wl_client *wl_client, void *data,
            uint32_t version, uint32_t id)
{
	struct tw_linux_dmabuf *dmabuf = data;
	struct wl_resource *resource =
		wl_resource_create(wl_client, &zwp_linux_dmabuf_v1_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource,
	                               &dmabuf_v1_impl, dmabuf,
	                               dmabuf_destroy_resource);
	dmabuf_send_formats(dmabuf, resource, version);
	//TODO if failed, we shall do something though
}

static void
notify_dmabuf_destroy(struct wl_listener *listener, void *data)
{
	struct tw_linux_dmabuf *dma =
		wl_container_of(listener, dma, destroy_listener);

	wl_global_destroy(dma->global);
	wl_list_remove(&dma->destroy_listener.link);
}

WL_EXPORT bool
tw_linux_dmabuf_init(struct tw_linux_dmabuf *dmabuf,
                     struct wl_display *display)
{
	dmabuf->global = wl_global_create(display,
	                                  &zwp_linux_dmabuf_v1_interface,
	                                  DMA_BUF_VERSION,
	                                  dmabuf,
	                                  bind_dmabuf);
	if (!dmabuf->global)
		return false;
	dmabuf->display = display;
	dmabuf->impl = NULL;
	dmabuf->impl_userdata = NULL;

	wl_list_init(&dmabuf->destroy_listener.link);
	dmabuf->destroy_listener.notify = notify_dmabuf_destroy;
	wl_display_add_destroy_listener(display, &dmabuf->destroy_listener);
	return true;
}

WL_EXPORT struct tw_linux_dmabuf *
tw_dmabuf_create_global(struct wl_display *display)
{
	struct tw_linux_dmabuf *dmabuf = &s_tw_linux_dmabuf;

        if (!tw_linux_dmabuf_init(dmabuf, display))
		return NULL;
	return dmabuf;
}
