/*
 * atoms.c - taiwins xwayland atoms collector
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

#include <string.h>
#include <taiwins/objects/logger.h>
#include "internal.h"

bool
collect_xwm_atoms(xcb_connection_t *conn, struct tw_xwm_atoms *xwm_atoms)
{
	struct {
		const char *name;
		xcb_atom_t *atom;
		xcb_intern_atom_cookie_t cookie;
	} atoms[] = {
		{ "WM_PROTOCOLS",	&xwm_atoms->wm_protocols },
		{ "WM_NORMAL_HINTS",	&xwm_atoms->wm_normal_hints },
		{ "WM_TAKE_FOCUS",	&xwm_atoms->wm_take_focus },
		{ "WM_DELETE_WINDOW",	&xwm_atoms->wm_delete_window },
		{ "WM_STATE",		&xwm_atoms->wm_state },
		{ "WM_S0",		&xwm_atoms->wm_s0 },
		{ "WM_CLIENT_MACHINE",	&xwm_atoms->wm_client_machine },
		{ "_NET_WM_CM_S0",	&xwm_atoms->net_wm_cm_s0 },
		{ "_NET_WM_NAME",	&xwm_atoms->net_wm_name },
		{ "_NET_WM_PID",	&xwm_atoms->net_wm_pid },
		{ "_NET_WM_ICON",	&xwm_atoms->net_wm_icon },
		{ "_NET_WM_STATE",	&xwm_atoms->net_wm_state },
		{ "_NET_WM_STATE_FOCUSED",
		  &xwm_atoms->net_wm_state_focused },
		{ "_NET_WM_STATE_MAXIMIZED_VERT",
		  &xwm_atoms->net_wm_state_maximized_vert },
		{ "_NET_WM_STATE_MAXIMIZED_HORZ",
		  &xwm_atoms->net_wm_state_maximized_horz },
		{ "_NET_WM_STATE_FULLSCREEN",
		  &xwm_atoms->net_wm_state_fullscreen },
		{ "_NET_WM_STATE_HIDDEN",
		  &xwm_atoms->net_wm_state_hidden },
		{ "_NET_WM_USER_TIME", &xwm_atoms->net_wm_user_time },
		{ "_NET_WM_ICON_NAME", &xwm_atoms->net_wm_icon_name },
		{ "_NET_WM_DESKTOP", &xwm_atoms->net_wm_desktop },
		{ "_NET_WM_WINDOW_TYPE", &xwm_atoms->net_wm_window_type },

		{ "_NET_WM_WINDOW_TYPE_DESKTOP",
		  &xwm_atoms->net_wm_window_type_desktop },
		{ "_NET_WM_WINDOW_TYPE_DOCK",
		  &xwm_atoms->net_wm_window_type_dock },
		{ "_NET_WM_WINDOW_TYPE_TOOLBAR",
		  &xwm_atoms->net_wm_window_type_toolbar },
		{ "_NET_WM_WINDOW_TYPE_MENU",
		  &xwm_atoms->net_wm_window_type_menu },
		{ "_NET_WM_WINDOW_TYPE_UTILITY",
		  &xwm_atoms->net_wm_window_type_utility },
		{ "_NET_WM_WINDOW_TYPE_SPLASH",
		  &xwm_atoms->net_wm_window_type_splash },
		{ "_NET_WM_WINDOW_TYPE_DIALOG",
		  &xwm_atoms->net_wm_window_type_dialog },
		{ "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
		  &xwm_atoms->net_wm_window_type_dropdown },
		{ "_NET_WM_WINDOW_TYPE_POPUP_MENU",
		  &xwm_atoms->net_wm_window_type_popup },
		{ "_NET_WM_WINDOW_TYPE_TOOLTIP",
		  &xwm_atoms->net_wm_window_type_tooltip },
		{ "_NET_WM_WINDOW_TYPE_NOTIFICATION",
		  &xwm_atoms->net_wm_window_type_notification },
		{ "_NET_WM_WINDOW_TYPE_COMBO",
		  &xwm_atoms->net_wm_window_type_combo },
		{ "_NET_WM_WINDOW_TYPE_DND",
		  &xwm_atoms->net_wm_window_type_dnd },
		{ "_NET_WM_WINDOW_TYPE_NORMAL",
		  &xwm_atoms->net_wm_window_type_normal },

		{ "_NET_WM_MOVERESIZE", &xwm_atoms->net_wm_moveresize },
		{ "_NET_SUPPORTING_WM_CHECK",
					&xwm_atoms->net_supporting_wm_check },
		{ "_NET_SUPPORTED",     &xwm_atoms->net_supported },
		{ "_NET_ACTIVE_WINDOW",     &xwm_atoms->net_active_window },
		{ "_MOTIF_WM_HINTS",	&xwm_atoms->motif_wm_hints },
		{ "CLIPBOARD",		&xwm_atoms->clipboard },
		{ "CLIPBOARD_MANAGER",	&xwm_atoms->clipboard_manager },
		{ "TARGETS",		&xwm_atoms->targets },
		{ "UTF8_STRING",	&xwm_atoms->utf8_string },
		{ "_WL_SELECTION",	&xwm_atoms->wl_selection },
		{ "INCR",		&xwm_atoms->incr },
		{ "TIMESTAMP",		&xwm_atoms->timestamp },
		{ "MULTIPLE",		&xwm_atoms->multiple },
		{ "UTF8_STRING"	,	&xwm_atoms->utf8_string },
		{ "COMPOUND_TEXT",	&xwm_atoms->compound_text },
		{ "TEXT",		&xwm_atoms->text },
		{ "STRING",		&xwm_atoms->string },
		{ "WINDOW",		&xwm_atoms->window },
		{ "text/plain;charset=utf-8",	&xwm_atoms->text_plain_utf8 },
		{ "text/plain",		&xwm_atoms->text_plain },
		{ "XdndSelection",	&xwm_atoms->xdnd_selection },
		{ "XdndAware",		&xwm_atoms->xdnd_aware },
		{ "XdndEnter",		&xwm_atoms->xdnd_enter },
		{ "XdndLeave",		&xwm_atoms->xdnd_leave },
		{ "XdndDrop",		&xwm_atoms->xdnd_drop },
		{ "XdndStatus",		&xwm_atoms->xdnd_status },
		{ "XdndFinished",	&xwm_atoms->xdnd_finished },
		{ "XdndTypeList",	&xwm_atoms->xdnd_type_list },
		{ "XdndActionCopy",	&xwm_atoms->xdnd_action_copy },
		{ "_XWAYLAND_ALLOW_COMMITS",	&xwm_atoms->allow_commits },
		{ "WL_SURFACE_ID",	&xwm_atoms->wl_surface_id }
	};
	const int n_atoms = sizeof(atoms) / sizeof(atoms[0]);
	xcb_intern_atom_reply_t *reply;
	xcb_generic_error_t *error;

	for (int i = 0; i < n_atoms; i++)
		atoms[i].cookie = xcb_intern_atom(conn, 0, strlen(atoms[i].name),
		                                  atoms[i].name);
	for (int i = 0; i < n_atoms; i++) {
		reply = xcb_intern_atom_reply(conn, atoms[i].cookie, &error);
		if (reply && !error)
			*atoms[i].atom = reply->atom;
		free(reply);
		if (error) {
			tw_logl_level(TW_LOG_WARN, "Could not get xcb_atom %s",
			              atoms[i].name);
			free(error);
			return false;
		}
	}
	return true;
}
