/*
 * serial_engine.h - taiwins serial engine interface
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

#ifndef TW_SERIAL_ENGINE_H
#define TW_SERIAL_ENGINE_H

#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define TW_SERIAL_DEPTH 16

struct tw_serial_slot {
	uint32_t serial;
	struct wl_list link;
};

/**
 * @brief serial engine is used to generate next serial number
 *
 * The engine is essenstially a queue with a fixed depth. We can use to verify
 * the emitted serial.
 */
struct tw_serial_engine {
	uint32_t curr_serial;
	struct wl_list serials;

	struct tw_serial_slot lasts[TW_SERIAL_DEPTH];
};

void
tw_serial_engine_init(struct tw_serial_engine *engine);

uint32_t
tw_serial_engine_next_serial(struct tw_serial_engine *engine);

bool
tw_serial_engine_verify_serial(struct tw_serial_engine *engine,
                               uint32_t serial);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
