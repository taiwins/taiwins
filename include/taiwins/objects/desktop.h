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

#include <stdint.h>
#include <stdlib.h>
#include <pixman.h>
#include <wayland-server.h>
#include "utils.h"

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
	/* requested from xwayland surfaces, the implementation need send back
	 * a configure event as return. It can return whatever as pleased, the
	 * simplest would be just return what ever sent here */
	void (*configure_requested)(struct tw_desktop_surface *surface,
	                            int x, int y, unsigned w, unsigned h,
	                            uint32_t flags, void *user_data);
};

enum tw_desktop_surface_type {
	TW_DESKTOP_TOPLEVEL_SURFACE = 1,
	TW_DESKTOP_TRANSIENT_SURFACE = 2,
	TW_DESKTOP_POPUP_SURFACE = 4,
};

enum tw_desktop_surface_state_flag {
	/* states */
	TW_DESKTOP_SURFACE_TILED_LEFT = 1 << 0,
	TW_DESKTOP_SURFACE_TILED_RIGHT = 1 << 1,
	TW_DESKTOP_SURFACE_TILED_TOP = 1 << 2,
	TW_DESKTOP_SURFACE_TILED_BOTTOM = 1 << 3,
	TW_DESKTOP_SURFACE_FOCUSED = 1 << 4,
	TW_DESKTOP_SURFACE_MAXIMIZED = 1 << 5,
	TW_DESKTOP_SURFACE_FULLSCREENED = 1 << 6,
	TW_DESKTOP_SURFACE_MINIMIZED = 1 << 7,
	TW_DESKTOP_SURFACE_STATES = (1 << 8) - 1, //all the states
	/* flags */
	TW_DESKTOP_SURFACE_CONFIG_X = 1 << 9,
	TW_DESKTOP_SURFACE_CONFIG_Y = 1 << 10,
	TW_DESKTOP_SURFACE_CONFIG_W = 1 << 11,
	TW_DESKTOP_SURFACE_CONFIG_H = 1 << 12,
};

struct tw_desktop_surface {
	struct wl_resource *resource; /**< shared by implementation */
	struct tw_surface *tw_surface;
	struct tw_desktop_manager *desktop;
	enum tw_desktop_surface_type type;
	bool surface_added;
	uint32_t states;
	struct tw_size_2d max_size, min_size;
        /**
         * the window geometry for this given desktop surface, always available
         * after every commit. The value before the initial commit is 0.
         */
	struct tw_geometry_2d window_geometry;
	char *title, *class;

	//API is required to call this function for additional size change. Xdg
	//API also send configure for popup, which sets the position as well.
	void (*configure)(struct tw_desktop_surface *surface,
	                  enum wl_shell_surface_resize edge,
	                  int32_t x, int32_t y,
	                  unsigned width, unsigned height, uint32_t flags);
	void (*close)(struct tw_desktop_surface *surface);
	void (*ping)(struct tw_desktop_surface *surface, uint32_t serial);

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

void
tw_desktop_surface_init(struct tw_desktop_surface *surf,
                        struct wl_resource *wl_surface,
                        struct wl_resource *resource,
                        struct tw_desktop_manager *desktop);
void
tw_desktop_surface_fini(struct tw_desktop_surface *surf);

void
tw_desktop_surface_add(struct tw_desktop_surface *surf);

void
tw_desktop_surface_rm(struct tw_desktop_surface *surf);

void
tw_desktop_surface_set_fullscreen(struct tw_desktop_surface *surf,
                                  struct wl_resource *output,
                                  bool fullscreen);
void
tw_desktop_surface_set_maximized(struct tw_desktop_surface *surf,
                                 bool maximized);
void
tw_desktop_surface_set_minimized(struct tw_desktop_surface *surf);

void
tw_desktop_surface_set_title(struct tw_desktop_surface *surf,
                             const char *title, size_t maxlen);
void
tw_desktop_surface_set_class(struct tw_desktop_surface *surf,
                             const char *class, size_t maxlen);
void
tw_desktop_surface_move(struct tw_desktop_surface *surf,
                        struct wl_resource *seat, uint32_t serial);
void
tw_desktop_surface_resize(struct tw_desktop_surface *surf,
                          struct wl_resource *seat, uint32_t edge,
                          uint32_t serial);
void
tw_desktop_surface_calc_window_geometry(struct tw_surface *surface,
                                        pixman_region32_t *geometry);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
