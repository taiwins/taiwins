/*
 * xwayland.h - taiwins xwayland header
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

#ifndef TW_XWAYLAND_H
#define TW_XWAYLAND_H

#include <wayland-server.h>
#include <taiwins/objects/subprocess.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xwm;
struct tw_desktop_surface;

struct tw_xserver {
	struct wl_display *wl_display;

	char name[16];
	int wms[2];
	int unix_fd;
	int abstract_fd; /**< only used by linux */
	struct wl_event_source *unix_source;
	struct wl_event_source *abstract_source;

	int display;
	pid_t pid;
	/* */
	struct wl_client *client;
	struct tw_subprocess process;
	struct tw_xwm *wm;
	struct wl_event_source *sigusr1_source;

	struct {
		struct wl_listener display_destroy;
		struct wl_listener client_destroy;
	} listeners;

	struct {
		struct wl_signal ready;
		struct wl_signal destroy;
	} signals;


};

struct tw_xserver *
tw_xserver_create_global(struct wl_display *display, bool lazy);

bool
tw_xserver_init(struct tw_xserver *server, struct wl_display *display,
                bool lazy);
void
tw_xserver_fini(struct tw_xserver *server);

void
tw_xserver_set_seat(struct tw_xserver *server, struct tw_data_device *device);

bool
tw_xserver_create_xwindow_manager(struct tw_xserver *server,
                                  struct tw_desktop_manager *desktop_manager,
                                  struct tw_compositor *compositor);
/**
 * xwayland surface is disgusted as a desktop surface for providing unified
 * APIs,
 */
struct tw_desktop_surface *
tw_xwayland_desktop_surface_from_tw_surface(struct tw_surface *surface);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
