/*
 * backend_libinput.c - tw_backend libinput device handlers
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
#include <ctypes/helpers.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <wayland-server.h>

#include <libinput.h>
#include <ctypes/helpers.h>

#include "backend.h"
#include "backend_internal.h"

/*
 * validate the libinput device by check whether it has a to z, this is not a
 * good solution I guess, but at least we are getting a keyboard.
 */
static bool
validate_keyboard(struct libinput_device *dev)
{
	uint32_t keycodes[] = {
		KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O,
		KEY_P, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K,
		KEY_L, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
	};

	for (unsigned i = 0; i < NUMOF(keycodes); i++) {
		if (libinput_device_keyboard_has_key(dev, keycodes[i]) == 0)
			return false;
	}
	return true;
}

static bool
validate_pointer(struct libinput_device *dev)
{
	return libinput_device_pointer_has_button(dev, BTN_LEFT) &&
		libinput_device_pointer_has_button(dev, BTN_RIGHT);
}

static bool
validate_touch(struct libinput_device *dev)
{
	return libinput_device_touch_get_touch_count(dev) > 0;
}

bool
tw_backend_valid_libinput_device(struct libinput_device *dev)
{
	if (libinput_device_has_capability(
		    dev, LIBINPUT_DEVICE_CAP_KEYBOARD))
		return validate_keyboard(dev);
	else if (libinput_device_has_capability(
		           dev, LIBINPUT_DEVICE_CAP_POINTER))
		return validate_pointer(dev);
	else if (libinput_device_has_capability(
		           dev, LIBINPUT_DEVICE_CAP_TOUCH))
		return validate_touch(dev);
	return true;
}
