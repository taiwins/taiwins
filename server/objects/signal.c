/*
 * signal.c - taiwins server safe signal handler
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

#include "signal.h"

static void
handle_noop(struct wl_listener *listener, void *data)
{
	//do nothing
}


/* adopted from wlroots, primary purpose is for providing safe env when
 * notifiers remove the from the list
 */
void
tw_signal_emit_safe(struct wl_signal *signal, void *data)
{
	//the algorithm works in this way:
	//
	//inserting two special guard at begining(cursor) and end(end). Then at
	//every iteration we shift cursor to the right BEFORE calling the
	//notifier(this is important), so you can remove any elements among
	//listeners, unless you removed cursor.
	struct wl_listener cursor;
	struct wl_listener end;

	wl_list_insert(&signal->listener_list, &cursor.link);
	cursor.notify = handle_noop;
	wl_list_insert(signal->listener_list.prev, &end.link);
	end.notify = handle_noop;

	while (cursor.link.next != &end.link) {
		struct wl_list *pos = cursor.link.next;
		struct wl_listener *l = wl_container_of(pos, l, link);

		wl_list_remove(&cursor.link);
		wl_list_insert(pos, &cursor.link);

		l->notify(l, data);
	}

	wl_list_remove(&cursor.link);
	wl_list_remove(&end.link);

}
