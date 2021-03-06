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

#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>

static void *
tw_default_alloc(size_t size, const struct wl_interface *interface)
{
	return calloc(1, size);
}

static void
tw_default_free(void *ptr, const struct wl_interface *interface)
{
	free(ptr);
}

const struct tw_allocator tw_default_allocator = {
	.alloc = tw_default_alloc,
	.free = tw_default_free,
};

WL_EXPORT void
tw_signal_setup_listener(struct wl_signal *signal,
                         struct wl_listener *listener,
                         wl_notify_func_t notify)
{
	assert(notify);
	assert(signal);
	assert(listener);

	wl_list_init(&listener->link);
	listener->notify = notify;
	wl_signal_add(signal, listener);
}

WL_EXPORT void
tw_set_resource_destroy_listener(struct wl_resource *resource,
                                 struct wl_listener *listener,
                                 wl_notify_func_t notify)
{
	assert(resource);
	assert(listener);
	assert(notify);

	wl_list_init(&listener->link);
	listener->notify = notify;
	wl_resource_add_destroy_listener(resource, listener);
}

WL_EXPORT void
tw_set_display_destroy_listener(struct wl_display *display,
                                struct wl_listener *listener,
                                wl_notify_func_t notify)
{
	assert(display);
	assert(listener);
	assert(notify);

	wl_list_init(&listener->link);
	listener->notify = notify;
	wl_display_add_destroy_listener(display, listener);
}

WL_EXPORT bool
tw_match_wl_resource_client(struct wl_resource *a, struct wl_resource *b)
{
	return wl_resource_get_client(a) == wl_resource_get_client(b);
}

static void
handle_noop(struct wl_listener *listener, void *data)
{
	//do nothing
}


/* adopted from wlroots, primary purpose is for providing safe env when
 * notifiers remove the from the list
 */
WL_EXPORT void
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

WL_EXPORT void
tw_resource_destroy_common(struct wl_client *c, struct wl_resource *r)
{
	wl_resource_destroy(r);
}
