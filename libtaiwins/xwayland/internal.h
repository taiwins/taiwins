/*
 * internal.h - taiwins xwayland internal header
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

#ifndef TW_XWAYLAND_INTERNAL_H
#define TW_XWAYLAND_INTERNAL_H

#include "options.h"

#include <stdlib.h>
#include <wayland-server.h>
#include <xcb/xcb.h>
#include <xcb/render.h>
#include <xcb/xproto.h>
#if _TW_HAS_XCB_ERRORS
#include <xcb/xcb_errors.h>
#endif
#if _TW_HAS_XCB_ICCCM
#include <xcb/xcb_icccm.h>
#endif

#include <taiwins/xwayland.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/data_device.h>

#include "atoms.h"
#include "selection.h"

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * use xlib normal_hints if ICCCM not available
 */
struct tw_xnormal_hints {
	uint32_t flags;
	int32_t x, y;
	int32_t width, height;
	int32_t min_width, min_height;
	int32_t max_width, max_height;
	int32_t width_inc, height_inc;
	int32_t min_aspect_num, min_aspect_den;
	int32_t max_aspect_num, max_aspect_den;
	int32_t base_width, base_height; /* desired size */
	int32_t win_gravity;
};

#define USPosition	(1L << 0)
#define USSize		(1L << 1)
#define PPosition	(1L << 2)
#define PSize		(1L << 3)
#define PMinSize	(1L << 4)
#define PMaxSize	(1L << 5)
#define PResizeInc	(1L << 6)
#define PAspect		(1L << 7)
#define PBaseSize	(1L << 8)
#define PWinGravity	(1L << 9)

struct tw_xmotif_wm_hints {
	uint32_t flags;
	uint32_t functions;
	uint32_t decorations;
	int32_t input_mode;
	uint32_t status;
};

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define _NET_WM_STATE_REMOVE	0
#define _NET_WM_STATE_ADD	1
#define _NET_WM_STATE_TOGGLE	2

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT 0
#define _NET_WM_MOVERESIZE_SIZE_TOP 1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT 2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT 3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT 4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM 5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT 6
#define _NET_WM_MOVERESIZE_SIZE_LEFT 7
#define _NET_WM_MOVERESIZE_MOVE 8  /* movement only */
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD 9  /* size via keyboard */
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD 10  /* move via keyboard */
#define _NET_WM_MOVERESIZE_CANCEL 11  /* cancel operation */

#define XCB_EVENT_TYPE_MASK (0x7f)


/** @brief the window manager for xwayland.
 *
 * The xwm works like any other normal X window manager by creating a root
 * window (as we told Xwayland to start in --rootless mode) and using
 * substructure_redirect to intercept all child window events to us to
 * process. For example:
 *
 * We intercept `ConfigureRequest` from clients, we may set different different
 * geometry using xcb_configure_window.
 */
struct tw_xwm {
	//wayland resources
	struct tw_xserver *server;
	struct wl_list surfaces;
	struct wl_event_source *x11_event;
	struct tw_desktop_manager *manager;
	struct tw_xsurface *focus_window;
	struct tw_xwm_selection selection, dnd;
	struct tw_seat *seat;
	struct {
		struct wl_listener server_destroy;
		struct wl_listener wl_surface_create;
		struct wl_listener wl_surface_focus;
		struct wl_listener seat_destroy;

	} listeners;

	///x resources
	//connections
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window; //root window

#if _TW_HAS_XCB_ERRORS
	xcb_errors_context_t *errors_context;
#endif
	const xcb_query_extension_reply_t *xfixes;
	//formats
	xcb_colormap_t colormap;
	xcb_visualid_t visual_id;
	xcb_render_pictformat_t format_rgba;

	struct tw_xwm_atoms atoms;
};

/**
 * @brief tw_xsurface implements a tw_desktop_surface for xcb_window
 *
 * Xwayland creates new wl_surface for every xcb window by sending a
 * wl_surface id. Here the job is mapping the xcb window logic to the
 * tw_desktop_surface API.
 *
 * xcb window would send a MapRequest event for mapping the surface on the
 * window manager, this usually happens before we actually receive the
 * wl_surface id.
 */
struct tw_xsurface {
	xcb_window_t id;

	/* The override-redirect specifies whether map and configure requests
	 * on this window should override a SubstructureRedirect on the parent,
	 * typically to inform a window manager not to tamper with the
	 * window. */
	bool override_redirect, pending_mapping;
	bool has_alpha, support_delete;
	bool decor_title, decor_border; /* TODO NEED support */

	pid_t pid;
	xcb_atom_t win_type;
	int x, y, w, h;
        /* we should either have a surface (mapped) or a surface_id
         * (unmapped). */
	uint32_t surface_id;
	struct tw_surface *surface;
	struct tw_xsurface *parent;
	struct tw_xwm *xwm;

	struct wl_list link; /* xwm:surfaces */
	struct wl_list children;
	struct wl_listener surface_destroy;

	struct tw_desktop_surface dsurf;
	struct tw_subsurface subsurface; /**< used if is subsurface */
};

struct tw_xsurface *
tw_xsurface_create(struct tw_xwm *xwm, xcb_window_t win_id,
                   int x, int y, unsigned w, unsigned h,
                   bool override_redirect);
void
tw_xsurface_destroy(struct tw_xsurface *surface);

struct tw_xsurface *
tw_xsurface_from_id(struct tw_xwm *xwm, xcb_window_t id);

void
tw_xsurface_map_requested(struct tw_xsurface *surface);

void
tw_xsurface_unmap_requested(struct tw_xsurface *surface);

void
tw_xsurface_map_tw_surface(struct tw_xsurface *surface,
                           struct tw_surface *tw_surface);
void
tw_xsurface_read_property(struct tw_xsurface *surface, xcb_atom_t type);

void
tw_xsurface_read_client_msg(struct tw_xsurface *surface,
                            xcb_client_message_event_t *ev);
void
tw_xsurface_read_config_request(struct tw_xsurface *surface,
                                xcb_configure_request_event_t *ev);
void
tw_xsurface_set_focus(struct tw_xsurface *surface, struct tw_xwm *xwm);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
