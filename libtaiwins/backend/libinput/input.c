/*
 * input.c - taiwins server input device libinput implemenation
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


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libinput.h>
#include <stdint.h>

#include <taiwins/backend.h>
#include <taiwins/backend_libinput.h>
#include <taiwins/input_device.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>

static const struct tw_libinput_impl dummy_impl = {
	.get_output_device = NULL,
};

/******************************************************************************
 * input device creation/destruction
 *****************************************************************************/

static inline uint32_t
parse_libinput_seat_id(struct libinput_seat *seat)
{
	uint32_t id = 0;
	const char *seat_name = libinput_seat_get_physical_name(seat);

	if (seat_name && (sscanf(seat_name, "seat%u", &id) == 1))
		return id;
	return 0;
}

static bool
tw_input_device_type_from_libinput(struct libinput_device *device,
                                   enum tw_input_device_type *type)
{
	if (libinput_device_has_capability(
		    device, LIBINPUT_DEVICE_CAP_KEYBOARD))
		*type = TW_INPUT_TYPE_KEYBOARD;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_POINTER))
		*type = TW_INPUT_TYPE_POINTER;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TOUCH))
		*type = TW_INPUT_TYPE_TOUCH;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_GESTURE))
		*type = TW_INPUT_TYPE_POINTER;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_SWITCH))
		*type = TW_INPUT_TYPE_SWITCH;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TABLET_PAD))
		*type = TW_INPUT_TYPE_TABLET_PAD;
	else if (libinput_device_has_capability(
		         device, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
		*type = TW_INPUT_TYPE_TABLET_TOOL;
	else
		return false;
	return true;
}

static void
handle_libinput_device_remove(struct tw_input_device *base)
{
	struct tw_libinput_device *device =
		wl_container_of(base, device, base);
	libinput_device_unref(device->libinput);
}

static const struct tw_input_device_impl libinput_device_impl = {
	.destroy = handle_libinput_device_remove,
};

static struct tw_libinput_device *
tw_libinput_device_new(struct libinput_device *libinput_dev,
                       struct tw_libinput_input *input)
{
	struct tw_libinput_device *dev = NULL;
	struct libinput_seat *seat = libinput_device_get_seat(libinput_dev);
	uint32_t seat_id = parse_libinput_seat_id(seat);
	const char *name = libinput_device_get_name(libinput_dev);
	enum tw_input_device_type type;

	bool valid = tw_input_device_type_from_libinput(libinput_dev, &type);

	if (!valid)
		return NULL;
	if (!(dev = calloc(1, sizeof(*dev))))
		return NULL;

	tw_input_device_init(&dev->base, type, seat_id, &libinput_device_impl);
	strncpy(dev->base.name, (name) ? name : "<unknown>",
	        sizeof(dev->base.name));

	wl_list_init(&dev->link);
	dev->base.vendor = libinput_device_get_id_vendor(libinput_dev);
	dev->base.product = libinput_device_get_id_product(libinput_dev);
	dev->input = input;
	dev->libinput = libinput_dev;
	libinput_device_set_user_data(libinput_dev, dev);
	libinput_device_ref(libinput_dev);

	wl_list_insert(input->devices.prev, &dev->link);
	wl_list_insert(input->backend->inputs.prev, &dev->base.link);
	if (input->backend->started)
		wl_signal_emit(&input->backend->signals.new_input, &dev->base);

	return dev;
}

static inline void
tw_libinput_device_destroy(struct tw_libinput_device *dev)
{
	if (!dev)
		return;
	wl_list_remove(&dev->link);
	tw_input_device_fini(&dev->base);
	free(dev);
}

/******************************************************************************
 * handlers
 *****************************************************************************/

extern bool
handle_device_event(struct libinput_event *event);

static bool
handle_input_event(struct libinput_event *event)
{
	bool handled = true;
	struct libinput *libinput =
		libinput_event_get_context(event);
	struct libinput_device *libinput_device =
		libinput_event_get_device(event);
	struct tw_libinput_input *input =
		libinput_get_user_data(libinput);

	switch(libinput_event_get_type(event)) {

	case LIBINPUT_EVENT_DEVICE_ADDED:
		tw_libinput_device_new(libinput_device, input);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		tw_libinput_device_destroy(
			libinput_device_get_user_data(libinput_device));
		break;
	default:
		handled = false;
		break;
	}
	return handled;
}

static inline void
handle_events(struct tw_libinput_input *input)
{
	struct libinput_event *event;

	while ((event = libinput_get_event(input->libinput))) {
		if (!handle_input_event(event))
			handle_device_event(event);
		libinput_event_destroy(event);
	}
}

static int
handle_dispatch_libinput(int fd, uint32_t mask, void *data)
{
	struct tw_libinput_input *input = data;

	if (libinput_dispatch(input->libinput) != 0) {
		tw_logl_level(TW_LOG_WARN, "Failed to dispatch libinput");
		return 0;
	}
	handle_events(input);
	return 0;
}

static void
libinput_log_func(struct libinput *libinput,
		  enum libinput_log_priority priority,
		  const char *format, va_list args)
{
	enum TW_LOG_LEVEL level = TW_LOG_INFO;

	switch (priority) {
	case LIBINPUT_LOG_PRIORITY_DEBUG:
		level = TW_LOG_DBUG;
		break;
	case LIBINPUT_LOG_PRIORITY_INFO:
		level = TW_LOG_INFO;
		break;
	case LIBINPUT_LOG_PRIORITY_ERROR:
		level = TW_LOG_WARN;
		break;
	}

	tw_logv_level(level, format, args);
}


/******************************************************************************
 * public API
 *****************************************************************************/

WL_EXPORT bool
tw_libinput_input_init(struct tw_libinput_input *input,
                       struct tw_backend *backend, struct wl_display *display,
                       struct libinput *libinput, const char *seat,
                       const struct tw_libinput_impl *impl)
{
	wl_list_init(&input->devices);
	input->display = display;
	input->libinput = libinput;
	input->backend = backend;
	input->disabled = false;
	input->impl = impl ? impl : &dummy_impl;
	libinput_set_user_data(libinput, input);

	libinput_log_set_handler(libinput, &libinput_log_func);

        if (libinput_udev_assign_seat(input->libinput, seat) != 0)
		return false;
        handle_events(input);

	return tw_libinput_input_enable(input);
}

WL_EXPORT bool
tw_libinput_input_enable(struct tw_libinput_input *input)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(input->display);
	int fd = libinput_get_fd(input->libinput);

	if (!input->event)
		input->event = wl_event_loop_add_fd(loop, fd,
		                                    WL_EVENT_READABLE,
		                                    handle_dispatch_libinput,
		                                    input);
	if (!input->event)
		return false;
	if (input->disabled) {
		libinput_resume(input->libinput);
		handle_events(input);
		input->disabled = false;
	}

	return true;
}

WL_EXPORT void
tw_libinput_input_disable(struct tw_libinput_input *input)
{
	if (input->disabled)
		return;
	wl_event_source_remove(input->event);
	input->event = NULL;
	libinput_suspend(input->libinput);
	handle_events(input);
	input->disabled = true;
}

WL_EXPORT void
tw_libinput_input_fini(struct tw_libinput_input *input)
{
	struct tw_libinput_device *dev, *dev_tmp;

	if (input->event) {
		wl_event_source_remove(input->event);
		input->event = NULL;
	}
	wl_list_for_each_safe(dev, dev_tmp, &input->devices, link)
		tw_libinput_device_destroy(dev);
}
