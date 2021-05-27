/*
 * text_input.h - taiwins server text input header
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

#ifndef TW_TEXT_INPUT_H
#define TW_TEXT_INPUT_H

#include <pixman.h>
#include <wayland-server.h>
#include <taiwins/objects/seat.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_text_input_request_type {
	TW_TEXT_INPUT_TOGGLE = 1 << 0,
	TW_TEXT_INPUT_SURROUNDING_TEXT = 1 << 1,
	TW_TEXT_INPUT_CHANGE_CAUSE = 1 << 2,
	TW_TEXT_INPUT_CONTENT_TYPE = 1 << 3,
	TW_TEXT_INPUT_CURSOR_RECTANGLE = 1 << 4,
};

enum tw_text_input_event_type {
	TW_TEXT_INPUT_PREEDIT = 1 << 0,
	TW_TEXT_INPUT_COMMIT_STRING = 1 << 1,
	TW_TEXT_INPUT_SURROUNDING_DELETE = 1 << 2,
};

struct tw_text_input_event {
	struct {
		const char *text;
		int32_t cursor_begin;
		int32_t cursor_end;
	} preedit;

	const char *commit_string;
	struct {
		uint32_t before_length, after_length;
	} surrounding_delete;

	uint32_t events;
};

struct tw_text_input_state {
	bool enabled;
	struct wl_resource *focused; /**< text-input active on this surface */

	struct {
		char *text;
		uint32_t cursor;
		uint32_t anchor;
	} surrounding;

	pixman_rectangle32_t cursor_rect;

	uint32_t change_cause, content_hint, content_purpose;
	uint32_t requests;
};

struct tw_text_input {
	struct wl_resource *resource;
	struct tw_seat *seat;
	struct wl_resource *focused;
	uint32_t serial;

	struct tw_text_input_state pending, current;
	struct wl_listener focus_listener;
};

struct tw_text_input_manager {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_listener display_destroy_listener;
};

void
tw_text_input_commit_event(struct tw_text_input *text_input,
                           struct tw_text_input_event *event);
/* get current focused text input from the seat */
struct tw_text_input *
tw_text_input_find_from_seat(struct tw_seat *seat);

bool
tw_text_input_manager_init(struct tw_text_input_manager *manager,
                           struct wl_display *display);
struct tw_text_input_manager *
tw_text_input_manager_create_global(struct wl_display *display);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
