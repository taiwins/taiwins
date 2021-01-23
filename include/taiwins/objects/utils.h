/*
 * signal.h - taiwins server safe signal handler
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

#ifndef TW_SIGNAL_H
#define TW_SIGNAL_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_size_2d {
	uint32_t w, h;
};

struct tw_postion_2d {
	int32_t x, y;
};

struct tw_geometry_2d {
	int32_t x, y;
	uint32_t w, h;
};

void
tw_signal_emit_safe(struct wl_signal *signal, void *data);

void
tw_signal_setup_listener(struct wl_signal *signal,
                         struct wl_listener *listener,
                         wl_notify_func_t notify);
void
tw_set_resource_destroy_listener(struct wl_resource *resource,
                                 struct wl_listener *listener,
                                 wl_notify_func_t notify);
void
tw_set_display_destroy_listener(struct wl_display *display,
                                struct wl_listener *listener,
                                wl_notify_func_t notify);
bool
tw_match_wl_resource_client(struct wl_resource *a, struct wl_resource *b);

void
tw_resource_destroy_common(struct wl_client *c, struct wl_resource *r);

#define  tw_reset_wl_list(link) \
	({ \
		wl_list_remove(link); \
		wl_list_init(link); })

/**
 * @brief custom allocator
 * type that using it would need to have an alloc field for using
 * tw_alloc_wl_resource_for_obj
 */
struct tw_allocator {
	void *(*alloc)(size_t size, const struct wl_interface *interface);
	void (*free)(void *addr, const struct wl_interface *interface);
};
extern const struct tw_allocator tw_default_allocator;

#define tw_create_wl_resource_for_obj(res, obj, client, id, ver, iface)   \
	({ \
		bool ret = true; \
		obj = calloc(1, sizeof(*obj)); \
		ret = obj != NULL; \
		if (ret) { \
			res = wl_resource_create(client, &iface, ver, id); \
			ret = ret && res != NULL; \
		} \
		if (!ret) \
			free(obj); \
		ret; \
	})

#define tw_alloc_wl_resource_for_obj(res, obj, client, id, ver, iface, alloc) \
	({ \
		bool ret = true; \
		alloc = alloc ? alloc : &tw_default_allocator; \
		obj = alloc->alloc(sizeof(*obj), &iface); \
		ret = obj != NULL; \
		if (ret) { \
			obj->alloc = alloc; \
			res = wl_resource_create(client, &iface, ver, id); \
			ret = ret && res != NULL; \
		} \
		if (!ret) \
			alloc->free(obj, &iface); \
		ret; \
	})

static inline uint32_t
tw_millihertz_to_ns(unsigned mHz)
{
	//mHz means how many cycles per 1000s, to get max precesion
	return mHz ? (1000000000000LL / mHz) : 0;
}

static inline uint64_t
tw_timespec_to_msec(const struct timespec *spec)
{
	return (int64_t)spec->tv_sec * 1000 + spec->tv_nsec / 1000000;
}

static inline uint32_t
tw_get_time_msec(clockid_t clk)
{
	struct timespec now;

	clock_gettime(clk, &now);
	return tw_timespec_to_msec(&now);
}


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
