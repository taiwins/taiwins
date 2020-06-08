/*
 * layers.c - taiwins server layers manager
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

#include <taiwins.h>
#include <wayland-util.h>
#include "layers.h"

static struct tw_layers_manager s_layers_manager = {0};


void
tw_layers_build_view_list(struct tw_layers_manager *manager)
{
	struct tw_layer *layer;
	struct tw_view *view;

	//from top to bottom
	wl_list_for_each(layer, &manager->layers, link) {
		wl_list_for_each(view, &layer->views, link) {

		}
	}

}

struct tw_layers_manager *
tw_layers_manager_create_global(struct wl_display *display)
{
	return &s_layers_manager;
}
