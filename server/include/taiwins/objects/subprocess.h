/*
 * subprocess.h - taiwins subprocess handler
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

#ifndef TW_SUBPROCESS_H
#define TW_SUBPROCESS_H

#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_subprocess {
	pid_t pid;
	struct wl_list link;
	void *user_data;
	void (*chld_handler)(struct tw_subprocess *proc, int status);
};

struct wl_list *tw_get_clients_head();

/**
 * @brief front end of tw_launch_client_complex
 *
 * works like tw_launch_client_complex(ec, path, chld, NULL, NULL);
 */
struct wl_client *
tw_launch_client(struct wl_display *display, const char *path,
                 struct tw_subprocess *chld);

/**
 * @brief launch wayland client
 *
 * this function follows the fork-exec routine and creates a new wayland client,
 * setting wayland socket is taking care of and you can optionally set your own
 * fork and exec routine.
 *
 * The optional fork routine is done after fork() is called. It can be used to
 * setup the post forking procedures for parent and child process.
 *
 * The optional exec routine need to actually call exec*() and return the
 * non-zero if it fails.
 */
struct wl_client *
tw_launch_client_complex(struct wl_display *display, const char *path,
                         struct tw_subprocess *chld,
                         int (*fork)(pid_t, struct tw_subprocess *),
                         int (*exec)(const char *, struct tw_subprocess *));

void
tw_end_client(struct wl_client *client);



#ifdef  __cplusplus
}
#endif

#endif /* EOF */
