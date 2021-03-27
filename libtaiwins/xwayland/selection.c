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

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xproto.h>

#include "internal.h"


static const uint32_t EVENT_VALUE[] = {
	XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
	XCB_EVENT_MASK_PROPERTY_CHANGE
};

static inline struct tw_xwm_selection *
xwm_selection_from_type(struct tw_xwm *xwm, xcb_atom_t type)
{
	if (type == xwm->atoms.clipboard)
		return &xwm->selection;
	else if (type == xwm->atoms.xdnd_selection)
		return &xwm->dnd;
	else
		return NULL;
}


/* static inline struct tw_xwm_selection * */
/* tw_xwm_selection_from_window(struct tw_xwm *xwm, xcb_window_t window) */
/* { */
/*	if (window == xwm->selection.window) */
/*		return &xwm->selection; */
/*	else if (window == xwm->dnd.window) */
/*		return &xwm->dnd; */
/*	else */
/*		return NULL; */
/* } */

/******************************************************************************
 * selection notify handling
 *****************************************************************************/

static void
selection_add_data_source(struct tw_xwm_selection *selection)
{
	struct tw_xwm *xwm = selection->xwm;
	struct tw_data_device *device = selection->seat;
	struct tw_xwm_data_source *source = &selection->source;

	tw_xwm_data_source_reset(source);
        if (tw_xwm_data_source_get_targets(source, xwm))
	        tw_data_device_set_selection(device, &source->wl_source);
}

static void
selection_write_data_source(struct tw_xwm_selection *selection)
{
	tw_xwm_data_transfer_start_write(&selection->write_transfer);
}

/* handle wl_client request a xwindow data_source */
static void
handle_selection_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_selection_notify_event_t *ev = (xcb_selection_notify_event_t *) ge;

	if (ev->target == XCB_ATOM_NONE) {
		tw_log_level(TW_LOG_DBUG, "convert selection failed");
	} else if (ev->target == xwm->atoms.targets) {
		//comming from xfixes_selection_notify,
		if (!xwm->focus_window) {
			tw_logl_level(TW_LOG_DBUG, "deny write access, "
			              "no xwayland surface focused");
			return;
		}
		selection_add_data_source(&xwm->selection);
	} else {
		//coming from wl_data_offer.receive, writing to a data_offer
		selection_write_data_source(&xwm->selection);
	}
}

/******************************************************************************
 * selection request handling
 *****************************************************************************/

static void
selection_send_targets(struct tw_xwm_selection *selection,
                       xcb_selection_request_event_t *req)
{
	//wl_data_source
	struct tw_xwm *xwm = selection->xwm;
	struct tw_data_source *wl_source = selection->wl_source;
	size_t i = 0;
	char **mime_ptr;
	size_t n = 2 + wl_source->mimes.size / sizeof(char *);
	xcb_atom_t targets[n];

	targets[0] = xwm->atoms.timestamp;
	targets[1] = xwm->atoms.targets;
	wl_array_for_each(mime_ptr, &wl_source->mimes) {
		if (*mime_ptr) //we could have empty name
			targets[2+i++] = xwm_mime_name_to_atom(xwm, *mime_ptr);
	}
	xcb_change_property(xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    req->requestor,
	                    req->property,
	                    XCB_ATOM_NONE,
	                    32, //formats
	                    2+i,
	                    targets);
	tw_xwm_selection_send_notify(xwm, req, true);

}

static inline void
selection_send_timestamp(struct tw_xwm_selection *selection,
                         xcb_selection_request_event_t *req)
{
	xcb_change_property(selection->xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    req->requestor, req->property,
	                    XCB_ATOM_INTEGER,
	                    32, //format
	                    1, &selection->timestamp);
	tw_xwm_selection_send_notify(selection->xwm, req, true);
}

static void
selection_send_data(struct tw_xwm_selection *selection,
                    xcb_selection_request_event_t *req, const char *mime)
{
	int fd = -1;
	struct tw_data_source *wl_source = selection->wl_source;

	if (selection->read_transfer.fd >= 0) {
		tw_logl_level(TW_LOG_WARN, "clipboard in use");
		tw_xwm_selection_send_notify(selection->xwm, req, false);
	}
	fd = tw_xwm_data_transfer_init_read(&selection->read_transfer,
	                                    selection, req);
	if (fd < 0) {
		tw_logl_level(TW_LOG_WARN, "failed to connect wl_data_source");
		tw_xwm_selection_send_notify(selection->xwm, req, false);
	}
	tw_data_source_send_send(wl_source, mime, fd);
	tw_xwm_data_transfer_start_read(&selection->read_transfer);
}

static const char *
selection_check_mime(struct tw_xwm_selection *selection, xcb_atom_t target)
{
	char **mime_ptr = NULL;
	struct wl_array *mimes = &selection->wl_source->mimes;
	char *requested = xwm_mime_atom_to_name(selection->xwm, target);

	wl_array_for_each(mime_ptr, mimes) {
		if (*mime_ptr && (strcmp(*mime_ptr, requested) == 0)) {
			free(requested);
			return *mime_ptr;
		}
	}
	free(requested);
	return NULL;
}

/* handle when a xwindow requests the wl_data_source. We acts as the selection
 * onwer. This involves creating a fake data offer to read the wl_data_source
 * (in chunks or one shot), use the data to write to requestor's property it
 * asked
 */
static void
handle_selection_request(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_selection_request_event_t *ev =
		(xcb_selection_request_event_t *)ge;
	struct tw_xwm_selection *selection =
		xwm_selection_from_type(xwm, ev->selection);

	tw_logl_level(TW_LOG_DBUG, "xcb selection request event");
	//user asking what is left in the clipboard manager
	if (ev->selection == xwm->atoms.clipboard_manager) {
		//we do the same as weston and wlroots, because the target is
		//already converted
		tw_xwm_selection_send_notify(xwm, ev, true);
	} else if (selection == NULL) {
		tw_logl_level(TW_LOG_DBUG, "received request for unknown "
		              "selection");
		goto send_none_notify;
	} else if (selection->window != ev->owner) {
		tw_logl_level(TW_LOG_DBUG, "not our business");
		goto send_none_notify;
	} else if (xwm->focus_window == NULL) {
		char *name = xwm_get_atom_name(xwm, selection->type);
		tw_logl_level(TW_LOG_DBUG, "deny access to selection %s, "
		              "no xwayland surface focused", selection->type);
		free(name);
		goto send_none_notify;
	}

	if (ev->target == xwm->atoms.targets) {
		selection_send_targets(selection, ev);
	} else if (ev->target == xwm->atoms.timestamp) {
		selection_send_timestamp(selection, ev);
	} else {
		const char *mime = selection_check_mime(selection, ev->target);
		if (!mime) {
			tw_logl_level(TW_LOG_WARN, "failed selection request "
			              "unknown atom %u", ev->target);
			goto send_none_notify;
		}
		selection_send_data(selection, ev, mime);
	}
send_none_notify:
	//no data for the requestor but let it know
	tw_xwm_selection_send_notify(xwm, ev, false);
}

/******************************************************************************
 * selection request handling
 *****************************************************************************/

static int
handle_selection_property_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)ge;
	struct tw_xwm_selection *selection = &xwm->selection;

	if (ev->window == selection->window) {
		if (ev->state == XCB_PROPERTY_NEW_VALUE &&
		    ev->atom == xwm->atoms.wl_selection &&
		    selection->write_transfer.in_chunk) {
			//write new chunk
			return 0;
		}
	}

	if (ev->window == selection->read_transfer.req.requestor) {
		if (ev->state == XCB_PROPERTY_DELETE &&
		    ev->atom == selection->read_transfer.req.property &&
		    selection->read_transfer.in_chunk) {
			//read new chunk
			return 1;
		}
	}
	return 0;
}

/*
 * we moniters the selection owner changes for xfixes, the owner of the event
 * could be (new owner if set_owner is called) or null(if application closes).
 * If none, we basically clear the selection.
 */
static int
handle_xfixes_selection_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_xfixes_selection_notify_event_t *ev =
		(xcb_xfixes_selection_notify_event_t *)ge;
	struct tw_xwm_selection *selection =
		xwm_selection_from_type(xwm, ev->selection);
	tw_logl_level(TW_LOG_DBUG, "XCB_XFIXIES_SELECTION notify");

	if (!selection)
		return 0;
	if (ev->owner == XCB_WINDOW_NONE) {
		//TODO, no owner, we need to cleanup the selection in wayland
		selection->owner = XCB_WINDOW_NONE;
		return 1;
	}
	selection->owner = ev->owner;
	if (ev->owner == selection->window) {
		selection->timestamp = ev->timestamp;
		return 1;
	}
	//we are asking the owner to write the targets onto wl_selection, which
	//would in turn lead us to get_selection_target for creating
	//wl_data_source
	xcb_convert_selection(xwm->xcb_conn, selection->window,
	                      selection->type, //clipboard or dnd
	                      xwm->atoms.targets,
	                      xwm->atoms.wl_selection,
	                      ev->timestamp);
	xcb_flush(xwm->xcb_conn);
	return 1;
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
		//notify the owner of the selection done
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
clipboard_init(struct tw_xwm_selection *selection, struct tw_xwm *xwm)
{
	selection->xwm = xwm;
	selection->window = xcb_generate_id(xwm->xcb_conn);
	selection->type = xwm->atoms.clipboard;
	//setup listeners
	wl_list_init(&selection->source_set.link);
	wl_list_init(&selection->source_removed.link);
	/* tw_data_source_init(&selection->source, NULL, &selection_impl); */

	xcb_create_window(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
	                  selection->window, xwm->screen->root,
	                  0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  xwm->screen->root_visual, XCB_CW_EVENT_MASK,
	                  EVENT_VALUE);
}

static inline void
dnd_init(struct tw_xwm_selection *dnd, struct tw_xwm *xwm)
{
	uint32_t dnd_ver = 5;

	dnd->xwm = xwm;
	dnd->window = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
	                  dnd->window, xwm->screen->root,
	                  0, 0, 8192, 8192, 0,
	                  XCB_WINDOW_CLASS_INPUT_ONLY,
	                  xwm->screen->root_visual, XCB_CW_EVENT_MASK,
	                  EVENT_VALUE);
	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE, dnd->window,
	                    xwm->atoms.xdnd_aware, XCB_ATOM_ATOM,
	                    32, 1, &dnd_ver);
}

static inline void
clipboard_manager_init(struct tw_xwm_selection *selection, struct tw_xwm *xwm)
{
	//xwayland backend also acts the role of clipboard manager, refer
	//https://freedesktop.org/wiki/ClipboardManager/

	//we become the clipboard manager by declaring as the owner of the atom
	xcb_set_selection_owner(xwm->xcb_conn, selection->window,
	                        xwm->atoms.clipboard_manager,
	                        XCB_TIME_CURRENT_TIME);
}

static inline void
monitor_clipboard_event(struct tw_xwm_selection *selection,
                        struct tw_xwm *xwm)
{
	uint32_t mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	//xfixies allows us to deliver the event to selection->window, the
	//clipboard_manager
	xcb_xfixes_select_selection_input(xwm->xcb_conn,
	                                  selection->window,
	                                  xwm->atoms.clipboard, mask);
}

static inline void
notify_selection_wl_data_source_set(struct wl_listener *listener, void *data)
{
	struct tw_xwm_selection *selection =
		wl_container_of(listener, selection, source_set);
	selection->wl_source = data;
}

static inline void
notify_selection_wl_data_source_rm(struct wl_listener *listener, void *data)
{
	struct tw_xwm_selection *selection =
		wl_container_of(listener, selection, source_removed);
	selection->wl_source = NULL;
}

void
tw_xwm_selection_set_device(struct tw_xwm_selection *selection,
                            struct tw_data_device *device)
{
	if (device == selection->seat)
		return;

	//TODO properly handling the data source
	if (&selection->source.wl_source == device->source_set)
		tw_data_source_fini(&selection->source.wl_source);

	tw_reset_wl_list(&selection->source_set.link);
	tw_reset_wl_list(&selection->source_removed.link);

	tw_signal_setup_listener(&device->source_added,
	                         &selection->source_set,
	                         notify_selection_wl_data_source_set);
	tw_signal_setup_listener(&device->source_removed,
	                         &selection->source_removed,
	                         notify_selection_wl_data_source_rm);

	//TODO adding new data source to the new device
}

void
tw_xwm_init_selection(struct tw_xwm *xwm)
{
	clipboard_init(&xwm->selection, xwm);
	dnd_init(&xwm->dnd, xwm);
	clipboard_manager_init(&xwm->selection, xwm);
	monitor_clipboard_event(&xwm->selection, xwm);
}

void
tw_xwm_fini_selection(struct tw_xwm *xwm)
{
	if (xwm->selection.window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->selection.window);
		xwm->selection.window = 0;
	}
	if (xwm->dnd.window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->dnd.window);
		xwm->dnd.window = 0;
	}
}

void
tw_xwm_selection_send_notify(struct tw_xwm *xwm,
                             xcb_selection_request_event_t *req, bool set)
{
	xcb_selection_notify_event_t ev = {
		.response_type = XCB_SELECTION_NOTIFY,
		.sequence = 0,
		.time = req->time,
		.requestor = req->requestor,
		.selection = req->selection,
		.target = req->target,
		.property = set ? req->property : XCB_ATOM_NONE,
	};
	xcb_send_event(xwm->xcb_conn, 0, //propergate
	               req->requestor,
	               XCB_EVENT_MASK_NO_EVENT,
	               (const char *)&ev);
	xcb_flush(xwm->xcb_conn);
}
