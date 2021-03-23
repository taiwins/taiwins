/*
 * atoms.h - taiwins xwayland atoms header
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

#ifndef TW_XWAYLAND_ATOMS_H
#define TW_XWAYLAND_ATOMS_H

#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xwm;

struct tw_xwm_atoms {
	xcb_atom_t wm_protocols;
	xcb_atom_t wm_normal_hints;
	xcb_atom_t wm_take_focus;
	xcb_atom_t wm_delete_window;
	xcb_atom_t wm_state;
	xcb_atom_t wm_s0;
	xcb_atom_t wm_client_machine;
	xcb_atom_t net_wm_cm_s0;
	xcb_atom_t net_wm_name;
	xcb_atom_t net_wm_pid;
	xcb_atom_t net_wm_icon;
	xcb_atom_t net_wm_state; /* fullscreened, maximized, minimized */
	xcb_atom_t net_wm_state_maximized_vert;
	xcb_atom_t net_wm_state_maximized_horz;
	xcb_atom_t net_wm_state_fullscreen;
	xcb_atom_t net_wm_state_focused; /* whether window decoration drawing
	                                  * in the active state */
	xcb_atom_t net_wm_state_hidden;
	xcb_atom_t net_wm_user_time;
	xcb_atom_t net_wm_icon_name;
	xcb_atom_t net_wm_desktop;
	xcb_atom_t net_wm_window_type;
	xcb_atom_t net_wm_window_type_desktop;
	xcb_atom_t net_wm_window_type_dock;
	xcb_atom_t net_wm_window_type_toolbar;
	xcb_atom_t net_wm_window_type_menu;
	xcb_atom_t net_wm_window_type_utility;
	xcb_atom_t net_wm_window_type_splash;
	xcb_atom_t net_wm_window_type_dialog;
	xcb_atom_t net_wm_window_type_dropdown;
	xcb_atom_t net_wm_window_type_popup;
	xcb_atom_t net_wm_window_type_tooltip;
	xcb_atom_t net_wm_window_type_notification;
	xcb_atom_t net_wm_window_type_combo;
	xcb_atom_t net_wm_window_type_dnd;
	xcb_atom_t net_wm_window_type_normal;
	xcb_atom_t net_wm_moveresize;
	xcb_atom_t net_supporting_wm_check;
	xcb_atom_t net_supported;
	xcb_atom_t net_active_window; /* current active window */
	xcb_atom_t motif_wm_hints;
	xcb_atom_t clipboard;
	xcb_atom_t clipboard_manager;
	xcb_atom_t targets;
	xcb_atom_t utf8_string;
	xcb_atom_t wl_selection;
	xcb_atom_t incr;
	xcb_atom_t timestamp;
	xcb_atom_t multiple;
	xcb_atom_t compound_text;
	xcb_atom_t text;
	xcb_atom_t string;
	xcb_atom_t window;
	xcb_atom_t text_plain_utf8;
	xcb_atom_t text_plain;
	xcb_atom_t xdnd_selection;
	xcb_atom_t xdnd_aware;
	xcb_atom_t xdnd_enter;
	xcb_atom_t xdnd_leave;
	xcb_atom_t xdnd_drop;
	xcb_atom_t xdnd_status;
	xcb_atom_t xdnd_finished;
	xcb_atom_t xdnd_type_list;
	xcb_atom_t xdnd_action_copy;
	xcb_atom_t wl_surface_id;
	xcb_atom_t allow_commits;
};

bool
collect_xwm_atoms(xcb_connection_t *conn, struct tw_xwm_atoms *atoms);

xcb_atom_t
xwm_mime_name_to_atom(struct tw_xwm *xwm, const char *name);

char *
xwm_mime_atom_to_name(struct tw_xwm *xwm, xcb_atom_t atom);

char *
xwm_get_atom_name(struct tw_xwm *xwm, xcb_atom_t atom);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
