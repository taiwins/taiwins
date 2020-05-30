/*
 * taiwins.h - taiwins server shared header
 *
 * Copyright (c) 2019 Xichen Zhou
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

#ifndef TAIWINS_H
#define TAIWINS_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctypes/helpers.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <libweston/libweston.h>


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define MAX_WORKSPACE 8


#ifdef  __cplusplus
extern "C" {
#endif

struct tw_config;

/*******************************************************************************
 * logging functions
 ******************************************************************************/
extern FILE *tw_logfile;

int
tw_log(const char *format, va_list args);

static inline int
tw_logl(const char *format, ...)
{
	int ret;
	va_list ap;
	va_start(ap, format);
	ret = tw_log(format, ap);
	va_end(ap);

	return ret;
}

/*******************************************************************************
 * desktop functions
 ******************************************************************************/

void
tw_lose_surface_focus(struct weston_surface *surface);

void
tw_focus_surface(struct weston_surface *surface);

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor);

/******************************************************************************
 * util functions
 *****************************************************************************/

static inline struct weston_output *
tw_get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;
	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}

static inline struct weston_seat *
tw_get_default_seat(struct weston_compositor *ec)
{
	if (wl_list_empty(&ec->seat_list))
		return NULL;
	//be careful with container of, it doesn't care the
	return container_of(ec->seat_list.next,
			    struct weston_seat, link);
}

static inline struct weston_surface *
tw_surface_from_resource(struct wl_resource *wl_surface)
{
	return (struct weston_surface *)wl_resource_get_user_data(wl_surface);
}

static inline struct weston_view *
tw_default_view_from_surface(struct weston_surface *surface)
{
	//return NULL if no view is
	return (surface->views.next != &surface->views) ?

		container_of(surface->views.next, struct weston_view, surface_link) :
		NULL;
}

static inline struct weston_view *
tw_view_from_surface_resource(struct wl_resource *wl_surface)
{
	return tw_default_view_from_surface(
		tw_surface_from_resource(wl_surface));
}

static inline void
tw_map_view(struct weston_view *view)
{
	view->surface->is_mapped = true;
	view->is_mapped = true;
}


/******************************************************************************
 * wayland util functions
 *****************************************************************************/

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
tw_launch_client(struct weston_compositor *ec, const char *path,
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
tw_launch_client_complex(struct weston_compositor *ec, const char *path,
                         struct tw_subprocess *chld,
                         int (*fork)(pid_t, struct tw_subprocess *),
                         int (*exec)(const char *, struct tw_subprocess *));

void
tw_end_client(struct wl_client *client);

int
tw_handle_sigchld(int sig_num, void *data);

int
tw_term_on_signal(int sig_num, void *data);

bool
tw_set_socket(struct wl_display *display);

/******************************************************************************
 * util functions
 *****************************************************************************/

enum tw_layout_type {
	LAYOUT_FLOATING,
	LAYOUT_TILING,
	LAYOUT_MAXMIZED,
	LAYOUT_FULLSCREEN,
};

/**
 * @brief taiwins layer is tailered for desktop arrangement.
 *
 * the layer system is good for organize the the stacking order. Which view goes
 * to where, views have a "recent order" order as well and that is indeed
 * different than stacking order, have another recent_link for views would be a
 * good idea.
 */
enum tw_layer_pos {
	TW_LAYER_POS_HIDDEN = 0x00000000,
	TW_LAYER_POS_BACKGROUND = 0x00000001,

	/* some desktop icons can live here */
	TW_LAYER_POS_DESKTOP_BELOW_UI = 0x30000000,

	/* the back fullscreen layer, unfocused fullscreen layer will be in this
	 * layer. fullscreen front layer will have at most one view.
	 */
	TW_LAYER_POS_FULLSCREEN_BACK = 0x4fffffff,
	/*
	 * desktop layers: these (back, mid, front) layers are used by desktop
	 * applications. They should not interfere with other layers. Workspaces
	 * shall deal with all three layers at once, generates enough command
	 * for applying positions.
	 *
	 * usually tiling views seat in the mid layer, stacking/maximized view occupy
	 * the front layer if they are focused. If tiling views are focused, the front
	 * layer will have no views at all.
	 */
	TW_LAYRR_POS_DESKTOP_BACK = 0x500000000,
	TW_LAYER_POS_DESKTOP_MID = 0x50000001,
	TW_LAYER_POS_DESKTOP_FRONT = 0x500000002,

	TW_LAYER_POS_DESKTOP_UI = 0x800000000,

	/* see TW_LAYER_POS_FULLSCREEN_BACK */
	TW_LAYER_POS_FULLSCREEN_FRONT = 0x90000000,
	TW_LAYER_POS_CURSOR = 0xffffffff,
};

/**
 * @brief similar to weston_layer
 */
struct tw_layer {
	struct wl_list link;
	enum tw_layer_pos position;

	struct wl_list views;
};

void
tw_layer_set_position(struct tw_layer *layer, enum tw_layer_pos pos,
                      struct wl_list *layers);
void
tw_layer_unset_position(struct tw_layer *layer);

bool
tw_set_wl_surface(struct wl_client *client,
                  struct wl_resource *resource,
                  struct wl_resource *surface,
                  struct wl_resource *output,
                  struct wl_listener *surface_destroy_listener);

//two option to manipulate the view
void
setup_static_view(struct weston_view *view, struct weston_layer *layer,
                  int x, int y);
void
setup_ui_view(struct weston_view *view, struct weston_layer *layer,
              int x, int y);

/******************************************************************************
 * backend
 *****************************************************************************/


/******************************************************************************
 * libweston interface functions
 *****************************************************************************/

void *
tw_load_weston_module(const char *name, const char *entrypoint);

//the declarations we need to move back
void
weston_output_move(struct weston_output *output, int x, int y);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
