/*
 * serial_engine.c - taiwins serial engine interface
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
#include <wayland-util.h>
#include <taiwins/objects/serial_engine.h>


WL_EXPORT void
tw_serial_engine_init(struct tw_serial_engine *engine)
{
	engine->curr_serial = 0;
	wl_list_init(&engine->serials);
	for (int i = 0; i < TW_SERIAL_DEPTH; i++) {
		engine->lasts[i].serial = 0;
		wl_list_init(&engine->lasts[i].link);
		wl_list_insert(engine->serials.prev, &engine->lasts[i].link);
	}
}

WL_EXPORT uint32_t
tw_serial_engine_next_serial(struct tw_serial_engine *engine)
{
	struct tw_serial_slot *slot =
		wl_container_of(engine->serials.prev, slot, link);
	wl_list_remove(&slot->link);
	engine->curr_serial += 1;
	slot->serial = engine->curr_serial;
	wl_list_insert(&engine->serials, &slot->link);
	return engine->curr_serial;
}

WL_EXPORT bool
tw_serial_engine_verify_serial(struct tw_serial_engine *engine,
                               uint32_t serial)
{
	struct tw_serial_slot *slot = NULL;
	wl_list_for_each(slot, &engine->serials, link) {
		if (slot->serial == serial)
			return true;
	}
	return false;
}
