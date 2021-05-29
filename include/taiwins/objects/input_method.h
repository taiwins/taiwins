/*
 * input_method.h - taiwins server input method header
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

#ifndef TW_INPUT_METHOD_H
#define TW_INPUT_METHOD_H

#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/subsurface.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_input_method_request_type {
	TW_INPUT_METHOD_PREEDIT = 1 << 0,
	TW_INPUT_METHOD_COMMIT_STRING = 1 << 1,
	TW_INPUT_METHOD_SURROUNDING_DELETE = 1 << 2,
};

enum tw_input_method_event_type {
	TW_INPUT_METHOD_TOGGLE = 1 << 0,
	TW_INPUT_METHOD_SURROUNDING_TEXT = 1 << 1,
	TW_INPUT_METHOD_CHANGE_CAUSE = 1 << 2,
	TW_INPUT_METHOD_CONTENT_TYPE = 1 << 3,
	TW_INPUT_METHOD_CURSOR_RECTANGLE = 1 << 4,
};

struct tw_input_method_event {
	bool enabled;
	/** activated text input */
	struct wl_resource *focused;

	struct {
		uint32_t cursor, anchor;
		const char *text;
	} surrounding;
	pixman_rectangle32_t cursor_rect;

	uint32_t change_cause, content_hint, content_purpose;
	uint32_t events;
};

struct tw_input_method_state {
	struct {
		char *text;
		int32_t cursor_begin, cursor_end;
	} preedit;
	char *commit_string;
	struct {
		uint32_t before_length, after_length;
	} surrounding_delete;
	uint32_t requests;
};

struct tw_input_method {
	struct wl_resource *resource;

	/* tw_input_method_manager:resources */
	struct wl_list link;
	struct tw_seat *seat;

	struct {
		struct tw_subsurface subsurface;
		pixman_rectangle32_t rectangle;
	} im_surface;

	struct {
		struct wl_resource *focused;
		struct wl_listener destroy;
	} text_input;

	struct tw_seat_keyboard_grab im_grab;
	struct tw_input_method_state pending, current;
	struct wl_listener seat_destroy_listener;
};

struct tw_input_method_manager {
	struct wl_display *display;
	struct wl_listener display_destroy_listener;
	struct wl_global *global;
	struct wl_list ims;
};

void
tw_input_method_send_event(struct tw_input_method *im,
                           struct tw_input_method_event *event);
struct tw_input_method *
tw_input_method_find_from_seat(struct tw_seat *seat);

struct tw_input_method_manager *
tw_input_method_manager_create_global(struct wl_display *display);

bool
tw_input_method_manager_init(struct tw_input_method_manager *manager,
                             struct wl_display *display);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
