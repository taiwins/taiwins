/*
 * backend_libinput.h - taiwins server input libinput functions
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

#ifndef TW_INPUT_LIBINPUT_INTERNAL_H
#define TW_INPUT_LIBINPUT_INTERNAL_H

#include <libudev.h>
#include <wayland-server.h>

#include "taiwins/input_device.h"
#include "taiwins/output_device.h"
#include "taiwins/backend.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_libinput_device {
	struct tw_input_device base;
	struct tw_libinput_input *input;
	struct libinput_device *libinput;

	struct wl_list link; /* tw_libinput_input: devices */
};

/** implement by backend for complete libinput functions */
struct tw_libinput_impl {
	struct tw_output_device *(*get_output_device)(struct udev_device *);
};

/**
 * this input hub uses by backend, designed to be autonomous, adding new input
 * devices by itself.
 */
struct tw_libinput_input {
	struct tw_backend *backend;
	struct wl_display *display;
	struct libinput *libinput;
	struct wl_event_source *event;
	bool disabled;

	const struct tw_libinput_impl *impl;
	struct wl_list devices;
};

bool
tw_libinput_input_init(struct tw_libinput_input *input,
                       struct tw_backend *backend, struct wl_display *display,
                       struct libinput *libinput, const char *seat,
                       const struct tw_libinput_impl *impl);
bool
tw_libinput_input_enable(struct tw_libinput_input *input);

void
tw_libinput_input_disable(struct tw_libinput_input *input);

void
tw_libinput_input_fini(struct tw_libinput_input *input);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
