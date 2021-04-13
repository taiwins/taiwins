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

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <taiwins/objects/layers.h>

static struct tw_layers_manager s_layers_manager = {0};

static void
notify_display_destroy(struct wl_listener *listener, void *data)
{
	//there is nothing to do right now.
}

WL_EXPORT void
tw_layers_manager_init(struct tw_layers_manager *manager,
                       struct wl_display *display)
{
	wl_list_init(&manager->layers);
	wl_list_init(&manager->views);
	wl_list_init(&manager->destroy_listener.link);
	tw_layer_init(&manager->cursor_layer);

	manager->display = display;
	manager->destroy_listener.notify = notify_display_destroy;
	wl_display_add_destroy_listener(display, &manager->destroy_listener);
	tw_layer_set_position(&manager->cursor_layer, TW_LAYER_POS_CURSOR,
	                      manager);
}

WL_EXPORT struct tw_layers_manager *
tw_layers_manager_create_global(struct wl_display *display)
{
	struct tw_layers_manager *manager = &s_layers_manager;
	tw_layers_manager_init(manager, display);
	return manager;
}

WL_EXPORT void
tw_layer_init(struct tw_layer *layer)
{
	wl_list_init(&layer->link);
	wl_list_init(&layer->views);
}


WL_EXPORT void
tw_layer_set_position(struct tw_layer *layer, enum tw_layer_pos pos,
                      struct tw_layers_manager *manager)
{
	struct tw_layer *l, *tmp;
	struct wl_list *layers = &manager->layers;
	wl_list_remove(&layer->link);
	wl_list_init(&layer->link);
	layer->position = pos;

	//from bottom to top
	wl_list_for_each_reverse_safe(l, tmp, layers, link) {
		if (l->position >= pos) {
			wl_list_insert(&l->link, &layer->link);
			return;
		}
	}
	wl_list_insert(layers, &layer->link);
}

WL_EXPORT void
tw_layer_unset_position(struct tw_layer *layer)
{
	wl_list_remove(&layer->link);
	wl_list_init(&layer->link);
}
