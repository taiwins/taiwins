/*
 * theme_lua.c - taiwins config bindings implementation
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

#include <helpers.h>
#include "config_internal.h"

/* TW_ZOOM_AXIS_BINDING */
void
zoom_axis(struct weston_pointer *pointer,
          UNUSED_ARG(const struct timespec *time),
          struct weston_pointer_axis_event *event, UNUSED_ARG(void *data))
{
	struct weston_compositor *ec = pointer->seat->compositor;
	double augment;
	struct weston_output *output;
	struct weston_seat *seat = pointer->seat;

	wl_list_for_each(output, &ec->output_list, link) {
		if (pixman_region32_contains_point(&output->region,
						   wl_fixed_to_int(pointer->x),
						   wl_fixed_to_int(pointer->y), NULL))
		{
			float sign = (event->has_discrete) ? -1.0 : 1.0;

			if (event->axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
				augment = output->zoom.increment * sign * event->value / 20.0;
			else
				augment = 0.0;

			output->zoom.level += augment;

			if (output->zoom.level < 0.0)
				output->zoom.level = 0.0;
			else if (output->zoom.level > output->zoom.max_level)
				output->zoom.level = output->zoom.max_level;

			if (!output->zoom.active) {
				if (output->zoom.level <= 0.0)
					continue;
				weston_output_activate_zoom(output, seat);
			}

			output->zoom.spring_z.target = output->zoom.level;
			weston_output_update_zoom(output);
		}
	}
}

/* TW_RELOAD_CONFIG_BINDING */
void
reload_config(UNUSED_ARG( struct weston_keyboard *keyboard ),
              UNUSED_ARG( const struct timespec *time ),
              UNUSED_ARG( uint32_t key ), UNUSED_ARG( uint32_t option ),
              void *data)
{
	struct tw_config *config = data;
	struct shell *shell = tw_config_request_object(config, "shell");

	if (!tw_config_run(config, NULL)) {
		const char *err_msg = tw_config_retrieve_error(config);
		shell_post_message(shell, TAIWINS_SHELL_MSG_TYPE_CONFIG_ERR, err_msg);
	}
}
