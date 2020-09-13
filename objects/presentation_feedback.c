/*
 * presentation.c - taiwins buffer implementation
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

#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-presentation-time-server-protocol.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/presentation_feedback.h>
#include <wayland-util.h>

#define PRESENTATION_VERSION 1


static const struct wp_presentation_interface presentation_impl;

static struct tw_presentation *
presentation_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wp_presentation_interface,
		&presentation_impl));
	return wl_resource_get_user_data(resource);
}

static void
tw_presentation_feedback_destroy(struct tw_presentation_feedback *feedback)
{
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &feedback->resources) {
		if (feedback->committed && !feedback->presented)
			wp_presentation_feedback_send_discarded(resource);
		wl_resource_destroy(resource);
	}

	wl_list_remove(&feedback->surface_destroy.link);
	wl_list_remove(&feedback->surface_commit.link);
	wl_list_remove(&feedback->link);
	free(feedback);
}

static void
feedback_destroy(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
notify_feedback_surface_destroy(struct wl_listener *listener, void *user_data)
{
	struct tw_presentation_feedback *feedback =
		wl_container_of(listener, feedback, surface_destroy);
	tw_presentation_feedback_destroy(feedback);
}

static void
notify_feedback_surface_commit(struct wl_listener *listener, void *user_data)
{
	struct tw_presentation_feedback *feedback =
		wl_container_of(listener, feedback, surface_commit);
	feedback->committed = true;
}

static struct tw_presentation_feedback *
presentation_feedback_find_create(struct tw_presentation *presentation,
                                  struct wl_client *client,
                                  struct tw_surface *surface,
                                  uint32_t version, uint32_t id)
{
	bool found = false;
	struct tw_presentation_feedback *feedback = NULL;
	struct wl_resource *resource = NULL;

	wl_list_for_each(feedback, &presentation->feedbacks, link) {
		if (feedback->surface == surface && !feedback->committed) {
			found = true;
			break;
		}
	}
	//create feedback.
	if (!found) {
		feedback = calloc(1, sizeof(struct tw_presentation_feedback));
		if (!feedback)
			return NULL;
		feedback->surface = surface;
		feedback->presentation = presentation;
		feedback->committed = false;
		feedback->presented = false;
		wl_list_init(&feedback->resources);
		wl_list_init(&feedback->link);
		wl_list_insert(presentation->feedbacks.prev, &feedback->link);

		tw_set_resource_destroy_listener(
			surface->resource, &feedback->surface_destroy,
			notify_feedback_surface_destroy);
		tw_signal_setup_listener(&surface->events.commit,
		                         &feedback->surface_commit,
		                         notify_feedback_surface_commit);
	}
	//create resource
	if (!(resource =
	      wl_resource_create(client, &wp_presentation_feedback_interface,
	                         version, id))) {
		if (!found)
			tw_presentation_feedback_destroy(feedback);
		return NULL;
	}
	wl_resource_set_implementation(resource, NULL, feedback,
	                               feedback_destroy);
	wl_list_insert(&feedback->resources, wl_resource_get_link(resource));
	return feedback;
}

static void
handle_presentation_feedback(struct wl_client *client,
                             struct wl_resource *presentation_resource,
                             struct wl_resource *surface_resource, uint32_t id)
{
	uint32_t version = wl_resource_get_version(presentation_resource);
	struct tw_presentation *presentation =
		presentation_from_resource(presentation_resource);
	struct tw_surface *surface =
		tw_surface_from_resource(surface_resource);
	struct tw_presentation_feedback *feedback =
		presentation_feedback_find_create(presentation, client,
		                                  surface, version, id);
	if (!feedback) {
		wl_resource_post_no_memory(presentation_resource);
		return;
	}
}

static void
handle_presentation_destroy(struct wl_client *client,
                            struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wp_presentation_interface presentation_impl = {
	.feedback = handle_presentation_feedback,
	.destroy = handle_presentation_destroy,
};


static void
bind_presentation(struct wl_client *client, void *data,
                  uint32_t version, uint32_t id)
{
	struct tw_presentation *presentation = data;
	struct wl_resource *resource =
		wl_resource_create(client, &wp_presentation_interface,
		                   version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &presentation_impl,
	                               presentation, NULL);
	wp_presentation_send_clock_id(resource, presentation->clock_id);
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_presentation *presentation =
		wl_container_of(listener, presentation, display_destroy);

	wl_global_destroy(presentation->global);
	presentation->global = NULL;
	presentation->display = NULL;
}

bool
tw_presentation_init(struct tw_presentation *presentation,
                     struct wl_display *display)
{
	presentation->global =
		wl_global_create(display,
		                 &wp_presentation_interface,
		                 PRESENTATION_VERSION, presentation,
		                 bind_presentation);
	if (!presentation->global)
		return false;
	presentation->display = display;
	presentation->clock_id = CLOCK_MONOTONIC;
	tw_set_display_destroy_listener(display,
	                                &presentation->display_destroy,
	                                handle_display_destroy);
	wl_list_init(&presentation->feedbacks);
	return true;
}

struct tw_presentation*
tw_presentation_create_global(struct wl_display *display)
{
	static struct tw_presentation s_presentation = {0};
	if (s_presentation.global)
		return &s_presentation;
	if (!tw_presentation_init(&s_presentation, display))
		return NULL;

	return &s_presentation;
}

void
tw_presentation_feeback_sync(struct tw_presentation_feedback *feedback,
                             struct wl_resource *output,
                             struct timespec *timespec)
{
	struct wl_resource *resource, *tmp;
	uint32_t tv_sec_hi = timespec->tv_sec >> 32;
	uint32_t tv_sec_lo = timespec->tv_sec & 0xFFFFFFFF;
	uint32_t tv_nsec = timespec->tv_nsec;
	uint32_t seq_hi = 0;
	uint32_t seq_lo = 0;
	uint32_t refresh = 0;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC;

	if (!feedback->committed)
		return;
	wl_resource_for_each_safe(resource, tmp, &feedback->resources) {
		wp_presentation_feedback_send_sync_output(resource, output);
		wp_presentation_feedback_send_presented(resource,
		                                        tv_sec_hi, tv_sec_lo,
		                                        tv_nsec,
		                                        refresh, seq_hi,
		                                        seq_lo,
		                                        flags);
	}
	feedback->presented = true;
	tw_presentation_feedback_destroy(feedback);
}
