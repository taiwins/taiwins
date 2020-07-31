/*
 * shell_layer.c - implementation of wlr_layer_shell
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

#include <assert.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wayland-wlr-layer-shell-server-protocol.h>

#include <objects/surface.h>
#include <objects/layers.h>

#include "backend/backend.h"
#include "desktop/shell.h"
#include "shell_internal.h"

#define LAYER_SHELL_VERSION 1
#define LAYER_SHELL_SURFACE_VERSION 3

static const struct zwlr_layer_shell_v1_interface layer_shell_impl;
static const struct zwlr_layer_surface_v1_interface layer_surface_impl;


/******************************************************************************
 * interfaces
 *****************************************************************************/

static void
calculate_geometry(void *user_data)
{
	struct tw_shell_ui *ui = user_data;
	struct wl_display *display = ui->shell->display;
	struct tw_backend_output *output = ui->output->output;
	uint32_t x = ui->pending.x;
	uint32_t y = ui->pending.y;
	uint32_t w = ui->pending.w;
	uint32_t h = ui->pending.h;
	uint32_t serial = wl_display_next_serial(display);
	pixman_rectangle32_t space =
		tw_shell_output_available_space(ui->shell, output);

	if (ui->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
		y = space.y;
	if (ui->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
		x = space.y;
	x += ui->pending.margin.x1;
	y += ui->pending.margin.y1;

	if (ui->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
		w = space.width;
	if (ui->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
		h = space.width;
	w -= ui->pending.margin.x2;
	h -= ui->pending.margin.y2;
	ui->pending.x = x;
	ui->pending.y = y;
	ui->pending.w = w;
	ui->pending.h = h;
	zwlr_layer_surface_v1_send_configure(ui->resource, serial, w, h);
}

static void
shell_ui_calculate_geometry_idle(struct tw_shell_ui *ui)
{
	struct wl_display *display = ui->shell->display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	wl_event_loop_add_idle(loop, calculate_geometry, ui);

}

static struct tw_shell_ui *
shell_ui_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwlr_layer_surface_v1_interface,
	                               &layer_surface_impl));
	return wl_resource_get_user_data(resource);
}

static void
resource_handle_destroy(struct wl_client *client,
                        struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
layer_surface_handle_set_size(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t width, uint32_t height)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);
	ui->pending.w = width;
	ui->pending.h = height;
	if (!width || !height)
		shell_ui_calculate_geometry_idle(ui);
}

static void
layer_surface_handle_ack_configure(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t serial)
{
	//TODO: proper implementation needs to search for the configure sent
	//then verify the configure, we dont have that resource here
}

static void
layer_surface_handle_set_anchor(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t anchor)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);
	uint32_t max_anchor = (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
	                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

	if (anchor > max_anchor) {
		wl_resource_post_error(
			resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR,
			"invalid anchor %d", anchor);
		return;
	}
	//if I have anchor but no size, I can't actually do anything.
	ui->pending.anchor = anchor;
	shell_ui_calculate_geometry_idle(ui);
}

static void
layer_surface_handle_set_exclusive_zone(struct wl_client *client,
                                        struct wl_resource *resource,
                                        int32_t zone)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);
	if (ui)
		ui->pending.occlusion_zone = zone;
	//if you have multiple those surfaces, organizing the layer shell would
	//not be fun.
}
static void
layer_surface_set_keyboard_interactivity(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t keyboard_interactivity)
{}

static void
layer_surface_handle_set_margin(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t top, int32_t right,
                                int32_t bottom, int32_t left)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);
	ui->pending.margin.x1 = left;
	ui->pending.margin.x2 = right;
	ui->pending.margin.y1 = top;
	ui->pending.margin.y2 = bottom;
	shell_ui_calculate_geometry_idle(ui);
}

static void
layer_surface_handle_get_popup(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *popup)
{
	//TODO: implement this
	wl_resource_post_error(
		resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
		"get_popup not implemented");
}

static void
layer_surface_set_layer(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t layer)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		ui->pending.layer = &ui->shell->background_layer;
		break;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		ui->pending.layer = &ui->shell->bottom_ui_layer;
		break;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		ui->pending.layer = &ui->shell->ui_layer;
		break;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		ui->pending.layer = &ui->shell->locker_layer;
		break;
	default:
		wl_resource_post_error(resource,
		                       ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
		                       "Invalid layer %d", layer);
		return;
	}
	//create ui maybe
}

static const struct zwlr_layer_surface_v1_interface layer_surface_impl = {
	.destroy = resource_handle_destroy,
	.ack_configure = layer_surface_handle_ack_configure,
	.set_size = layer_surface_handle_set_size,
	.set_anchor = layer_surface_handle_set_anchor,
	.set_exclusive_zone = layer_surface_handle_set_exclusive_zone,
	.set_margin = layer_surface_handle_set_margin,
	.set_keyboard_interactivity = layer_surface_set_keyboard_interactivity,
	.get_popup = layer_surface_handle_get_popup,
	.set_layer = layer_surface_set_layer,
};

static void
layer_surface_destroy_resource(struct wl_resource *resource)
{
	struct tw_shell_ui *ui = shell_ui_from_resource(resource);

	free(ui);
}

static inline struct tw_shell *
tw_shell_from_layer_shell_resoruce(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwlr_layer_shell_v1_interface,
	                               &layer_shell_impl));
	return wl_resource_get_user_data(resource);
}

static void
commit_layer_surface(struct tw_surface *surface)
{
	struct tw_shell_ui *ui = surface->role.commit_private;

	if (ui->pending.layer && ui->pending.layer != ui->layer) {
		ui->layer = ui->pending.layer;
		wl_list_remove(&surface->links[TW_VIEW_LAYER_LINK]);
		wl_list_init(&surface->links[TW_VIEW_LAYER_LINK]);
		wl_list_insert(&ui->layer->views,
		               &surface->links[TW_VIEW_LAYER_LINK]);
		tw_surface_dirty_geometry(surface);
	}
	//need to calculate the location based on provided info.
	tw_surface_set_position(surface, ui->pending.x, ui->pending.y);
	ui->x = ui->pending.x;
	ui->y = ui->pending.y;
}

static void
layer_shell_handle_get_layer_surface(struct wl_client *wl_client,
                                     struct wl_resource *client_resource,
                                     uint32_t id,
                                     struct wl_resource *surface_resource,
                                     struct wl_resource *output_resource,
                                     uint32_t layer, const char *namespace)
{
	struct tw_shell *shell =
		tw_shell_from_layer_shell_resoruce(client_resource);
	struct tw_backend_output *output;
	struct wl_resource *layer_surface_resource;
	struct tw_shell_ui *shell_ui = calloc(1, sizeof(*shell_ui));
	struct tw_shell_output *shell_output;
	struct tw_surface *surface =
		tw_surface_from_resource(surface_resource);
	struct tw_layer *l = &shell->bottom_ui_layer;

	if (!shell_ui) {
		wl_resource_post_no_memory(client_resource);
		return;
	}
	if (!output_resource)
		output = tw_backend_focused_output(shell->backend);
	else
		output = tw_backend_output_from_resource(output_resource);
	shell_output = shell_output_from_backend_output(shell, output);

	layer_surface_resource =
		wl_resource_create(wl_client,
		                   &zwlr_layer_surface_v1_interface,
		                   LAYER_SHELL_SURFACE_VERSION, id);
	if (!layer_surface_resource) {
		free(shell_ui);
		wl_resource_post_no_memory(client_resource);
		return;
	}
	wl_resource_set_implementation(layer_surface_resource,
	                               &layer_surface_impl,
	                               shell_ui,
	                               layer_surface_destroy_resource);

	shell_create_ui_element(shell, shell_ui, layer_surface_resource,
	                        surface, shell_output, 0, 0,
	                        l, commit_layer_surface);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_impl = {
	.get_layer_surface = layer_shell_handle_get_layer_surface,
};

/******************************************************************************
 * globals
 *****************************************************************************/

static void
bind_layer_shell(struct wl_client *client, void *data, uint32_t version,
                 uint32_t id)
{
	struct tw_shell *shell = data;
	struct wl_resource *res =
		wl_resource_create(client, &zwlr_layer_shell_v1_interface,
		                   version, id);
	if (res) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &layer_shell_impl, shell,
	                               NULL);
}

bool
shell_impl_layer_shell(struct tw_shell *shell, struct wl_display *display)
{
	shell->layer_shell = wl_global_create(display,
	                                      &zwlr_layer_shell_v1_interface,
	                                      LAYER_SHELL_VERSION, shell,
	                                      bind_layer_shell);
	if (!shell->layer_shell)
		return false;
	return true;
}
