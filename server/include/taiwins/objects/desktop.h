/*
 * destkop.h - taiwins wayland destkop protocols
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

#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include <stdlib.h>
#include <pixman.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_desktop_init_option {
	TW_DESKTOP_INIT_INCLUDE_WL_SHELL = 1 << 0,
	TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL = 1 << 1,
};


struct tw_desktop_surface;

struct tw_desktop_surface_api {
	void (*ping_timeout)(struct tw_desktop_surface *client,
			     void *user_data);
	void (*pong)(struct tw_desktop_surface *client,
		     void *user_data);

	void (*surface_added)(struct tw_desktop_surface *surface,
			      void *user_data);
	void (*surface_removed)(struct tw_desktop_surface *surface,
				void *user_data);
	void (*committed)(struct tw_desktop_surface *surface,
			  void *user_data);
	void (*show_window_menu)(struct tw_desktop_surface *surface,
				 struct wl_resource *seat,
	                         int32_t x, int32_t y,
				 void *user_data);
	void (*set_parent)(struct tw_desktop_surface *surface,
			   struct tw_desktop_surface *parent,
			   void *user_data);
	void (*move)(struct tw_desktop_surface *surface,
		     struct wl_resource *seat, uint32_t serial,
	             void *user_data);
	void (*resize)(struct tw_desktop_surface *surface,
		       struct wl_resource *seat, uint32_t serial,
		       enum wl_shell_surface_resize edges, void *user_data);
	void (*fullscreen_requested)(struct tw_desktop_surface *surface,
	                             struct wl_resource *output,
	                             bool fullscreen, void *user_data);

	void (*maximized_requested)(struct tw_desktop_surface *surface,
				    bool maximized, void *user_data);
	void (*minimized_requested)(struct tw_desktop_surface *surface,
				    void *user_data);
};


enum tw_desktop_surface_type {
	TW_DESKTOP_TOPLEVEL_SURFACE = 1,
	TW_DESKTOP_TRANSIENT_SURFACE = 2,
	TW_DESKTOP_POPUP_SURFACE = 4,
};

struct tw_desktop_surface {
	struct wl_resource *resource; /**< shared by implementation */
	struct tw_surface *tw_surface;
	struct tw_desktop_manager *desktop;
	enum tw_desktop_surface_type type;
	bool fullscreened;
	bool maximized;
	bool surface_added;
	char *title, *class;
        /**
         * the window geometry for this given desktop surface, always available
         * after every commit. The value before the initial commit is 0.
         */
        struct {
	        int x, y;
	        unsigned int w, h;
	} window_geometry;

	//API is required to call this function for additional size change. Xdg
	//API also send configure for popup, which sets the position as well.
	void (*configure)(struct tw_desktop_surface *surface,
	                  enum wl_shell_surface_resize edge,
	                  int32_t x, int32_t y,
	                  unsigned width, unsigned height);
	void (*close)(struct tw_desktop_surface *surface);

	void *user_data;
};

struct tw_desktop_manager {
	struct wl_display *display;
	struct wl_global *wl_shell_global;
	struct wl_global *xdg_shell_global;
	struct tw_desktop_surface_api api;

	struct wl_listener destroy_listener;
	void *user_data;
};

bool
tw_desktop_init(struct tw_desktop_manager *desktop,
                struct wl_display *display,
                const struct tw_desktop_surface_api *api,
                void *user_data,
                enum tw_desktop_init_option option);

struct tw_desktop_manager *
tw_desktop_create_global(struct wl_display *display,
                         const struct tw_desktop_surface_api *api,
                         void *user_data,
                         enum tw_desktop_init_option option);
/**
 * @brief getting the desktop surface from tw_surface
 *
 * This function checks if tw_surface is a wl_shell_surface or xdg_surface,
 * returned surface could be a toplevel, transient or a popup, additional check
 * needs to be done.
 */
struct tw_desktop_surface *
tw_desktop_surface_from_tw_surface(struct tw_surface *surface);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
