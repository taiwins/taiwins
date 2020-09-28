/*
 * output.c - taiwins server x11 backend output implementation
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

#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "backend/x11.h"
#include "internal.h"
#include "output_device.h"
#include "render_context.h"
#include "taiwins/objects/logger.h"

#define DEFAULT_REFRESH (60 * 1000) /* 60Hz */
#define FRAME_DELAY (1000000 / DEFAULT_REFRESH)

static int
frame_handler(void *data)
{
	struct tw_x11_output *output = data;
	wl_signal_emit(&output->device.events.new_frame, &output->device);
	wl_event_source_timer_update(output->frame_timer,
	                             FRAME_DELAY);
	return 0;
}

static void
parse_output_setup(struct tw_x11_output *output, xcb_connection_t *xcb)
{
	const xcb_setup_t *xcb_setup = xcb_get_setup(xcb);

	snprintf(output->device.make, sizeof(output->device.make), "%.*s",
	         xcb_setup_vendor_length(xcb_setup),
	         xcb_setup_vendor(xcb_setup));
	snprintf(output->device.model, sizeof(output->device.model),
	         "%"PRIu16".%"PRIu16,
	         xcb_setup->protocol_major_version,
	         xcb_setup->protocol_minor_version);
}

static void
x11_remove_output(struct tw_x11_output *output)
{
	struct tw_x11_backend *x11 = output->x11;

        wl_list_remove(&output->device.link);
        wl_event_source_remove(output->frame_timer);

	wl_signal_emit(&output->device.events.destroy, &output->device);
	tw_render_surface_fini(&output->render_surface, x11->ctx);
	xcb_destroy_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);
	free(output);
}

bool
tw_x11_output_start(struct tw_x11_output *output)
{
	struct tw_x11_backend *x11 = output->x11;
	struct wl_event_loop *loop = wl_display_get_event_loop(x11->display);

	//for the given window
	uint32_t win_mask = XCB_CW_EVENT_MASK;
	uint32_t mask_values[] = {
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};
        //creating the window but not mapping it yet.
	output->win = xcb_generate_id(x11->xcb_conn);
	xcb_create_window(x11->xcb_conn, XCB_COPY_FROM_PARENT, output->win,
	                  x11->screen->root, 0, 0,
	                  output->width, output->height,
	                  1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  x11->screen->root_visual,
	                  win_mask, mask_values);

        //setup the input events for the output.
	struct {
		xcb_input_event_mask_t head;
		xcb_input_xi_event_mask_t mask;
	} xinput_mask = {
		.head = {
			.deviceid = XCB_INPUT_DEVICE_ALL_MASTER,
			.mask_len = 1
		},
		.mask = XCB_INPUT_XI_EVENT_MASK_KEY_PRESS |
			XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_MOTION |
			XCB_INPUT_XI_EVENT_MASK_ENTER |
			XCB_INPUT_XI_EVENT_MASK_LEAVE |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_END |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE,
	};

	xcb_input_xi_select_events(x11->xcb_conn, output->win, 1,
	                           &xinput_mask.head);
	//creating the render surface
	if (!tw_render_surface_init_window(&output->render_surface,
	                                   x11->ctx, &output->win)) {
		tw_logl_level(TW_LOG_WARN, "failed to create render surface "
		              "for X11 output");
		x11_remove_output(output);
		return false;
	}

	xcb_map_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);

	output->frame_timer = wl_event_loop_add_timer(loop, frame_handler,
	                                              output);
	wl_event_source_timer_update(output->frame_timer, FRAME_DELAY);

	//finally
	wl_signal_emit(&x11->impl.events.new_output, &output->device);

	return true;
}

bool
tw_x11_backend_add_output(struct tw_backend *backend,
                          unsigned int width, unsigned int height)
{
	struct tw_x11_backend *x11 =
		wl_container_of(backend->impl, x11, impl);
	struct tw_x11_output *output =
		calloc(1, sizeof(*output));

        if (!output)
		return false;
        output->x11 = x11;
        output->width = width;
        output->height = height;

        tw_output_device_init(&output->device);
        sprintf(output->device.name, "X11-%d", wl_list_length(&x11->outputs));
        parse_output_setup(output, x11->xcb_conn);

        wl_list_insert(x11->outputs.prev, &output->device.link);

        if (backend->started)
	        tw_x11_output_start(output);


	return true;
}
