/*
 * xsurface.h - taiwins xwayland surface internal header
 *
 * Copyright (c) 2021 Xichen Zhou
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

#ifndef TW_XSURFACE_INTERNAL_H
#define TW_XSURFACE_INTERNAL_H


#include "options.h"
#include "xwm.h"

#ifdef  __cplusplus
extern "C" {
#endif

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
tw_xsurface_map_tw_surface(struct tw_xsurface *surface,
                           struct tw_surface *tw_surface);
int
tw_xsurface_handle_event(struct tw_xwm *xwm, xcb_generic_event_t *ge);

void
tw_xsurface_set_focus(struct tw_xsurface *surface, struct tw_xwm *xwm);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
