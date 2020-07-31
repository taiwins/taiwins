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
#include <wayland-server-core.h>
#include <wayland-util.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef  __cplusplus
extern "C" {
#endif

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

#define  tw_reset_wl_list(link) \
	({ \
		wl_list_remove(link); \
		wl_list_init(link); })

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

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
