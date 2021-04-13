/*
 * output.h - taiwins wl_output protocol header
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

#ifndef TW_OUTPUT_H
#define TW_OUTPUT_H

#include <wayland-server-protocol.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_output {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list resources;
	uint32_t scale;
	int32_t x, y;

	struct {
		int phyw, phyh;
		enum wl_output_subpixel subpixel;
		enum wl_output_transform transform;
		char *make, *model;
	} geometry;

	struct {
		uint32_t flags;
		int width, height, refresh;
	} mode;

	struct wl_listener display_destroy_listener;
	char name[32];
};

struct tw_output *
tw_output_create(struct wl_display *display);

void
tw_output_destroy(struct tw_output *output);

struct tw_output *
tw_output_from_resource(struct wl_resource *resource);

void
tw_output_set_name(struct tw_output *output, const char *name);

void
tw_output_set_scale(struct tw_output *output, uint32_t scale);

void
tw_output_set_coord(struct tw_output *output, int x, int y);

void
tw_output_set_geometry(struct tw_output *output,
                       int physical_w, int physical_h,
                       char *make, char *model,
                       enum wl_output_subpixel subpixel,
                       enum wl_output_transform transform);
void
tw_output_set_mode(struct tw_output *output, uint32_t mode_flags,
                   int32_t width, int height, int refresh);
void
tw_output_send_clients(struct tw_output *output);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
