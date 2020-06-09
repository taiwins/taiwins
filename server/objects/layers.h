/*
 * layers.h - taiwins server layers manager
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

#ifndef TW_LAYERS_H
#define TW_LAYERS_H

#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_layout_type {
	LAYOUT_FLOATING,
	LAYOUT_TILING,
	LAYOUT_MAXMIZED,
	LAYOUT_FULLSCREEN,
};

/**
 * @brief taiwins layer is tailered for desktop arrangement.
 *
 * the layer system is good for organize the the stacking order. Which view goes
 * to where, views have a "recent order" order as well and that is indeed
 * different than stacking order, have another recent_link for views would be a
 * good idea.
 */
enum tw_layer_pos {
	TW_LAYER_POS_HIDDEN = 0x00000000,
	TW_LAYER_POS_BACKGROUND = 0x00000001,

	/* some desktop icons can live here */
	TW_LAYER_POS_DESKTOP_BELOW_UI = 0x30000000,

	/* the back fullscreen layer, unfocused fullscreen layer will be in this
	 * layer. fullscreen front layer will have at most one view.
	 */
	TW_LAYER_POS_FULLSCREEN_BACK = 0x4fffffff,
	/*
	 * desktop layers: these (back, mid, front) layers are used by desktop
	 * applications. They should not interfere with other layers. Workspaces
	 * shall deal with all three layers at once, generates enough command
	 * for applying positions.
	 *
	 * usually tiling views seat in the mid layer, stacking/maximized view occupy
	 * the front layer if they are focused. If tiling views are focused, the front
	 * layer will have no views at all.
	 */
	TW_LAYRR_POS_DESKTOP_BACK = 0x500000000,
	TW_LAYER_POS_DESKTOP_MID = 0x50000001,
	TW_LAYER_POS_DESKTOP_FRONT = 0x500000002,

	TW_LAYER_POS_DESKTOP_UI = 0x800000000,

	/* see TW_LAYER_POS_FULLSCREEN_BACK */
	TW_LAYER_POS_FULLSCREEN_FRONT = 0x90000000,
	TW_LAYER_POS_CURSOR = 0xffffffff,
};

/**
 * @brief similar to weston_layer
 */
struct tw_layer {
	struct wl_list link;
	enum tw_layer_pos position;

	struct wl_list views;
};

struct tw_layers_manager {
	struct wl_display *display;
	struct wl_list layers;
	struct wl_list views;

	struct wl_listener destroy_listener;
	//global layers
	struct tw_layer cursor_layer;
};

void
tw_layer_set_position(struct tw_layer *layer, enum tw_layer_pos pos,
                      struct wl_list *layers);
void
tw_layer_unset_position(struct tw_layer *layer);


struct tw_layers_manager *
tw_layers_manager_create_global(struct wl_display *display);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
