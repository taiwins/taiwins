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

#include <stdarg.h>
#include <helpers.h>
#include <wayland-server.h>
#include <libweston/zalloc.h>
#include <libweston/libweston.h>


#if defined (INCLUDE_DESKTOP)
#include <libweston-desktop/libweston-desktop.h>
#endif

#if defined (INCLUDE_BACKEND)
#include <libweston/backend-drm.h>
#include <libweston/backend-wayland.h>
#include <libweston/backend-x11.h>
#include <libweston/windowed-output-api.h>
#endif

#include <wayland-taiwins-shell-server-protocol.h>

#include "../shared_config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_config;

/*******************************************************************************
 * logging functions
 ******************************************************************************/
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

/**
 * @brief taiwins output information
 *
 * here we define some template structures. It is passed as pure data, and they
 * are not persistent. So don't store them as pointers.
 */
struct tw_output {
	struct weston_output *output;
	//available space used in desktop area. We don't have the configureation
	//code yet, once it is available, it can be used to create this struct.
	struct weston_geometry desktop_area;
	uint32_t inner_gap;
	uint32_t outer_gap;
};

void
tw_lose_surface_focus(struct weston_surface *surface);

void
tw_focus_surface(struct weston_surface *surface);

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor);

/*******************************************************************************
 * util functions
 ******************************************************************************/

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

/*******************************************************************************
 * shell functions
 ******************************************************************************/

struct shell;

/**
 * @brief annouce globals
 */
bool
tw_setup_shell(struct weston_compositor *compositor, const char *path,
               struct tw_config *config);

void
tw_shell_set_wallpaper(struct shell *shell, const char *wp);

void
tw_shell_set_widget_path(struct shell *shell, const char *path);

void
tw_shell_set_panel_pos(struct shell *shell, enum taiwins_shell_panel_pos pos);

void
tw_shell_set_menu(struct shell *shell, vector_t *menu);

struct shell *tw_shell_get_global();

/*******************************************************************************
 * console functions
 ******************************************************************************/

struct console;

bool
tw_setup_console(struct weston_compositor *compositor,
                 const char *exec_path,
                 struct tw_config *config);

struct console *tw_console_get_global();

/*******************************************************************************
 * desktop functions
 ******************************************************************************/
struct desktop;

bool
tw_setup_desktop(struct weston_compositor *compositor,
                 struct tw_config *config);

struct desktop *tw_desktop_get_global();

int
tw_desktop_num_workspaces(struct desktop *desktop);

const char *
tw_desktop_get_workspace_layout(struct desktop *desktop, unsigned int i);

bool
tw_desktop_set_workspace_layout(struct desktop *desktop, unsigned int i,
                                const char *layout);
void
tw_desktop_get_gap(struct desktop *desktop, int *inner, int *outer);

void
tw_desktop_set_gap(struct desktop *desktop, int inner, int outer);

/*******************************************************************************
 * theme functions
 ******************************************************************************/
struct theme;

bool
tw_setup_theme(struct weston_compositor *ec, struct tw_config *config);

struct theme *tw_theme_get_global();

struct tw_theme *
tw_theme_access_theme(struct theme *theme);

/*******************************************************************************
 * client functions
 ******************************************************************************/

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

/*******************************************************************************
 * bus functions
 ******************************************************************************/

bool
tw_setup_bus(struct weston_compositor *ec, struct tw_config *config);

/*******************************************************************************
 * backend functions
 ******************************************************************************/

struct tw_backend;
struct tw_backend_output;

bool
tw_setup_backend(struct weston_compositor *ec, struct tw_config *c);

struct tw_backend *tw_backend_get_global();

struct tw_backend_output *
tw_backend_output_from_weston_output(struct weston_output *output,
                                     struct tw_backend *b);
void
tw_backend_output_set_scale(struct tw_backend_output *output,
                                    unsigned int scale);
enum weston_compositor_backend
tw_backend_get_type(struct tw_backend *be);

void
tw_backend_output_set_transform(struct tw_backend_output *output,
                                enum wl_output_transform transform);
//TODO resolution

/*******************************************************************************
 * xwayland functions
 ******************************************************************************/
bool
tw_setup_xwayland(struct weston_compositor *ec, struct tw_config *config);

/*******************************************************************************
 * util functions
 ******************************************************************************/

bool tw_set_wl_surface(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *surface,
		       struct wl_resource *output,
		       struct wl_listener *surface_destroy_listener);

//two option to manipulate the view
void setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y);
void setup_ui_view(struct weston_view *view, struct weston_layer *layer, int x, int y);


/*******************************************************************************
 * libweston interface functions
 ******************************************************************************/

void *tw_load_weston_module(const char *name, const char *entrypoint);

//the declarations we need to move back
void
weston_output_move(struct weston_output *output, int x, int y);

#ifdef  __cplusplus
}
#endif



#endif /* EOF */
