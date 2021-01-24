/*
 * selection.c - taiwins xwayland selection implementation
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

#include <stdint.h>
#include <taiwins/objects/data_device.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xproto.h>
#include "internal.h"
#include "taiwins/objects/logger.h"

//user pressed ctrl-C
static void
handle_selection_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_selection_notify_event_t *ev = (xcb_selection_notify_event_t *) ge;

	if (ev->target == XCB_ATOM_NONE) {
		tw_log_level(TW_LOG_DBUG, "convert selection failed");
	} else if (ev->target == xwm->atoms.targets) {
		if (!xwm->focus_window) {
			tw_logl_level(TW_LOG_DBUG, "deny write access, "
			              "no xwayland surface focused");
			return;
		}
		//TODO get targets
	} else {
		//TODO get data
	}
}

////user pressed ctrl-V
static void
handle_selection_request(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{

}

static void
handle_selection_property_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{

}

static int
handle_xfixes_selection_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	return 0;
}


int
tw_xwm_handle_selection_event(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	//TODO: check for seat
	switch (ge->response_type & XCB_EVENT_TYPE_MASK) {
	case XCB_SELECTION_NOTIFY:
		handle_selection_notify(xwm, ge);
		return 1;
	case XCB_SELECTION_REQUEST:
		handle_selection_request(xwm, ge);
		return 1;
	case XCB_PROPERTY_NOTIFY:
		handle_selection_property_notify(xwm, ge);
		return 1;
	}
	switch (ge->response_type - xwm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		return handle_xfixes_selection_notify(xwm, ge);
	}
	return 0;
}

static inline void
selection_init(struct tw_xwm *xwm, xcb_window_t win, xcb_atom_t atom)
{
	uint32_t mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;

	xcb_xfixes_select_selection_input(xwm->xcb_conn, win, atom, mask);
}

void
tw_xwm_init_selection(struct tw_xwm *xwm)
{
	uint32_t values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE
	};
	uint32_t dnd_ver = 5;

	xwm->selection_win = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
	                  xwm->selection_win, xwm->screen->root,
	                  0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  xwm->screen->root_visual, XCB_CW_EVENT_MASK, values);
	xcb_set_selection_owner(xwm->xcb_conn, xwm->selection_win,
	                        xwm->atoms.clipboard_manager,
	                        XCB_TIME_CURRENT_TIME);
	//TODO primary selection support?
	selection_init(xwm, xwm->selection_win, xwm->atoms.clipboard);
	//init dnd window to be an large input only window
	xwm->dnd_win = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
	                  xwm->dnd_win, xwm->screen->root,
	                  0, 0, 8192, 8192, 0,
	                  XCB_WINDOW_CLASS_INPUT_ONLY,
	                  xwm->screen->root_visual, XCB_CW_EVENT_MASK, values);
	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE, xwm->dnd_win,
	                    xwm->atoms.xdnd_aware, XCB_ATOM_ATOM,
	                    32, 1, &dnd_ver);

}


void
tw_xwm_fini_selection(struct tw_xwm *xwm)
{
	if (xwm->selection_win) {
		xcb_destroy_window(xwm->xcb_conn, xwm->selection_win);
		xwm->selection_win = 0;
	}
	if (xwm->dnd_win) {
		xcb_destroy_window(xwm->xcb_conn, xwm->dnd_win);
		xwm->dnd_win = 0;
	}
}
