/*
 * surface.h - taiwins wl_surface headers
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

#ifndef TW_SURFACE_H
#define TW_SURFACE_H

#include <wayland-server-core.h>
#include <wayland-server.h>
#include <pixman.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define MAX_VIEW_LINKS 5

enum tw_surface_state {
	TW_SURFACE_ATTACHED = (1 << 0),
	TW_SURFACE_DAMAGED = (1 << 1),
	TW_SURFACE_BUFFER_DAMAGED = (1 << 2),
	TW_SURFACE_SCALED = (1 << 3),
	TW_SURFACE_FRAME_REQUESTED = (1 << 4),
	TW_SURFACE_COMMITED = (1 << 5),
};

struct tw_surface;
struct tw_surface_manager;

/**
 * @brief tw_surface_buffer represents a buffer texture for the surface.
 *
 * On server side, a surface shall only need one buffer(texture) to present on
 * the output. Buffer uploading happens at commit, server would also release the
 * previous committed buffer if not released.
 */
struct tw_surface_buffer {
	/* can be a wl_shm_buffer or dma buffer */
	struct wl_resource *resource;
	int width, height, stride;
	union {
		uint32_t id;
		void *ptr;
	} handle;
};

struct tw_event_buffer_uploading {
	struct tw_surface_buffer *buffer;
	pixman_region32_t *damages;
	struct wl_resource *wl_buffer;
	// dma buffer?
};

struct tw_view {
	struct tw_surface *surface;
	uint32_t output_mask;
	int32_t dx, dy; /**< wl_surface_attach, pending */
	int32_t buffer_scale;
	int32_t transform;
	struct wl_resource *buffer_resource;

	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque_region, input_region;

        /**
         * many types may need to use one of the links, eg: backend_ouput,
         * layer, compositor, input. Plane.
         */
	uint32_t link_pool;
	struct wl_list links[MAX_VIEW_LINKS];
};

struct tw_subsurface;
struct tw_surface {
	struct wl_resource *resource;
	struct tw_surface_manager *manager;
        /** the tw_surface::buffer is used to present; tw_surface::used_buffer
         * will always point to buffer or it will be none
         */
	struct tw_surface_buffer buffer, *used_buffer;

        /* view is similar t
         * current: commited;
         * pending: attached, without commit;
         * previous: last commit
         * rotating towards left.
         */
	struct tw_view *pending, *current, *previous;
	struct tw_view surface_states[3];

#ifdef TW_OVERLAY_PLANE
        /* previously I solved the overlapping output damage by giving the copy
         * to all outputs. Not sure if there is a better solution. If we do not
         * do plane assignment at all. Maybe except cursor, we can directly
         * reduce the damages to the output.
         */
	pixman_region32_t output_damages[32];
#endif

	struct wl_list frame_callbacks;
	/* there is also the presentation feedback, later */
	struct wl_list subsurfaces;
	/* subsurface changes on commit  */
	struct wl_list pending_subsurface;
	/* wl_surface_attach_buffer(sx, sy) */
	uint32_t state;
	int sx, sy;
	bool is_mapped;
	struct tw_subsurface *subsurface;

	struct {
		const char *name;
		void *commit_private;
		void (*commit)(struct tw_surface *surface);
	} role;

	struct {
		struct wl_signal frame;
		struct wl_signal commit;
		struct wl_signal destroy;
	} events;

	void *user_data;
};

struct tw_subsurface {
	struct wl_resource *resource;
	struct tw_surface *surface;
	struct tw_surface *parent;
	struct wl_list parent_link;
	struct wl_list parent_pending_link;
	struct wl_listener surface_destroyed;
	int32_t sx, sy; //relate to parent
	bool sync;
};

struct tw_region {
	struct wl_resource *resource;
	pixman_region32_t region;
};

/**
 * @brief centralized surface hub, taking care of common signals.
 */
struct tw_surface_manager {
	//signals generated for all surface for additional processing, for
	//example, wp_viewporter would take advantage of commit_signal.
	struct wl_signal surface_created_signal;
	struct wl_signal subsurface_created_signal;
	struct wl_signal region_created_signal;
	/* struct wl_signal surface_destroy_signal; */

	/* struct wl_signal commit_signal; */
	/* struct wl_signal frame_signal; */
	struct {
		void (*buffer_import)(struct tw_event_buffer_uploading *event,
		                      void *);
		void *callback;
	} buffer_import;
};

void
tw_surface_manager_init(struct tw_surface_manager *manager);

struct tw_surface*
tw_surface_create(struct wl_client *client, uint32_t version, uint32_t id,
                  struct tw_surface_manager *manager);

struct tw_surface *
tw_surface_from_resource(struct wl_resource *wl_surface);

bool
tw_surface_has_texture(struct tw_surface *surface);

struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t version,
                     uint32_t id, struct tw_surface *surface,
                     struct tw_surface *parent);
struct tw_region *
tw_region_create(struct wl_client *client, uint32_t version, uint32_t id);

struct tw_region *
tw_region_from_resource(struct wl_resource *wl_region);

void
tw_surface_buffer_release(struct tw_surface_buffer *buffer);

void
tw_surface_buffer_update(struct tw_surface_buffer *buffer,
                         struct wl_resource *resource,
                         pixman_region32_t *damage);

void
tw_surface_buffer_new(struct tw_surface_buffer *buffer,
                      struct wl_resource *resource);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
