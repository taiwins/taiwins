/*
 * presentation_feedback.h - taiwins server presentation feedback header
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

#ifndef TW_PRESENTATION_FEEDBACK_H
#define TW_PRESENTATION_FEEDBACK_H

#include <time.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_presentation {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_listener display_destroy;
	uint32_t clock_id;

	struct wl_list feedbacks;
};

struct tw_presentation_feedback {
	struct tw_presentation *presentation;
	struct tw_surface *surface;
	bool committed, presented;
	struct wl_list link;
	struct wl_list resources;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
	struct wl_listener present_sync;
};

bool
tw_presentation_init(struct tw_presentation *presentation,
                     struct wl_display *display);

struct tw_presentation*
tw_presentation_create_global(struct wl_display *display);

/* compositor call this function to send necessary feedbacks to clients, this
 * means the given surface already presented on the given output.
 */
void
tw_presentation_feeback_sync(struct tw_presentation_feedback *feedback,
                             struct wl_resource *output,
                             struct timespec *timespec,
                             uint64_t seq, uint32_t refresh, uint32_t flags);
void
tw_presentation_feedback_discard(struct tw_presentation_feedback *feedback);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
