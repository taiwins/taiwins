/*
 * idle_inbihit.c - taiwins idle-inhibit implementation
 *
 * Copyright (c) 2021 Xichen Zhou
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

#include <assert.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/idle_inhibit.h>
#include <wayland-idle-inhibit-server-protocol.h>
#include <wayland-util.h>

#define INHIBIT_VERSION 1

static inline struct tw_idle_inhibit_manager *
idle_inhibit_manager_from_resource(struct wl_resource *resource);

static void
handle_destroy_inhibitor(struct wl_client *client,
                         struct wl_resource *resource)
{
	struct tw_idle_inhibit_manager *mgr =
		idle_inhibit_manager_from_resource(resource);
	tw_reset_wl_list(wl_resource_get_link(resource));
	wl_signal_emit(&mgr->inhibitor_request, mgr);
}

static struct zwp_idle_inhibitor_v1_interface inhibitor_impl = {
	.destroy = handle_destroy_inhibitor,
};

static void
handle_create_inhibitor(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t id,
                        struct wl_resource *surface)
{
	uint32_t version = wl_resource_get_version(resource);
	struct tw_idle_inhibit_manager *mgr =
		idle_inhibit_manager_from_resource(resource);

	struct wl_resource *inhibitor_res =
		wl_resource_create(client, &zwp_idle_inhibitor_v1_interface,
		                   version, id);
	if (!inhibitor_res) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(inhibitor_res, &inhibitor_impl, mgr,
	                               NULL);
	wl_list_insert(mgr->inhibitors.prev,
	               wl_resource_get_link(inhibitor_res));
	wl_signal_emit(&mgr->inhibitor_request, mgr);
}

static const struct zwp_idle_inhibit_manager_v1_interface manager_impl = {
	.destroy = tw_resource_destroy_common,
	.create_inhibitor = handle_create_inhibitor,
};

static inline struct tw_idle_inhibit_manager *
idle_inhibit_manager_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_idle_inhibit_manager_v1_interface,
	                               &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void
handle_destroy_manager_resource(struct wl_resource *resource)
{
	//TODO
}

static void
bind_inhibit_manager(struct wl_client *client, void *data,
                     uint32_t version, uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client,
		                   &zwp_idle_inhibit_manager_v1_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data,
	                               handle_destroy_manager_resource);
}

static void
notify_inhibit_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_idle_inhibit_manager *mgr =
		wl_container_of(listener, mgr, display_destroy_listener);
	tw_reset_wl_list(&listener->link);
	wl_global_destroy(mgr->global);
}

WL_EXPORT bool
tw_idle_inhibit_manager_init(struct tw_idle_inhibit_manager *mgr,
                             struct wl_display *display)
{
	mgr->global = wl_global_create(display,
	                               &zwp_idle_inhibit_manager_v1_interface,
	                               INHIBIT_VERSION, mgr,
	                               bind_inhibit_manager);
	if (!mgr->global)
		return false;
	wl_list_init(&mgr->inhibitors);
	wl_signal_init(&mgr->inhibitor_request);
	tw_set_display_destroy_listener(display,
	                                &mgr->display_destroy_listener,
	                                notify_inhibit_display_destroy);
	return true;
}

WL_EXPORT struct tw_idle_inhibit_manager *
tw_idle_inhibit_manager_create_global(struct wl_display *display)
{
	static struct tw_idle_inhibit_manager mgr = {0};

	if (mgr.global)
		return &mgr;
	if (!tw_idle_inhibit_manager_init(&mgr, display))
		return NULL;
	return &mgr;
}
