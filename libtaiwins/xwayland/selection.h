/*
 * selection.h - taiwins xwayland selection header
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

#ifndef TW_XWAYLAND_SELECTION_H
#define TW_XWAYLAND_SELECTION_H

#include <xcb/xcb.h>
#include <wayland-util.h>
#include <wayland-server.h>
#include <taiwins/objects/data_device.h>
#include <xcb/xproto.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xwm;
struct tw_xwm_selection;

struct tw_xwm_data_transfer {
	int fd;
	bool in_chunk;
        struct tw_xwm_selection *selection;
	struct wl_event_source *event;

	//writing data
	size_t property_offset; //write start from the offset
        xcb_get_property_reply_t *property_reply;

	//reading data
	ssize_t cached;
	xcb_selection_request_event_t req;
	//we set the property and waiting for requestor to delete
	bool property_set;
	char *data;
};

struct tw_xwm_data_source {
	struct tw_xwm_selection *selection;
	struct wl_array mime_types;
	struct tw_data_source wl_source;
};

/*
 * the X11 selection
 *
 * In X, the selection is a global thing. There a different selections and can
 * query the "owner" window of it. Here we mainly care the CLIPBOARD
 * selection. Client A who wants ctrl-v first query the owner of CLIPBOARD, it
 * is done through `xcb_get_selection_owner`.
 *
 * The client A calls `xcb_convert_selection` for actual data transfer. It
 * tells the owner of the selection to convert the clipboard into a specific
 * target and write to the property of a specific window. Afterwards it listen
 * to the SelectionNotify event, it means the write is done.
 *
 * ref: https://www.uninformativ.de/blog/postings/2017-04-02/0/POSTING-en.html
 */
struct tw_xwm_selection {
	struct tw_xwm *xwm;
	xcb_atom_t type;
	xcb_window_t window;
	xcb_window_t owner;
	xcb_timestamp_t timestamp;
	//we support one transfer at a time, could simply use only one data
	struct tw_xwm_data_transfer write_transfer, read_transfer;
	//the x11 side source, managed by us
	struct tw_xwm_data_source source;
	//the wayland side source, not managed by us
	struct tw_data_source *wl_source;
};

void
tw_xwm_data_transfer_init_write(struct tw_xwm_data_transfer *transfer,
                                struct tw_xwm_selection *selection,
                                int fd);
void
tw_xwm_data_transfer_start_write(struct tw_xwm_data_transfer *transfer);

void
tw_xwm_data_transfer_write_chunk(struct tw_xwm_data_transfer *transfer);

/* init the read transfer and return a fd for wl_data_source */
int
tw_xwm_data_transfer_init_read(struct tw_xwm_data_transfer *transfer,
                               struct tw_xwm_selection *selection,
                               xcb_selection_request_event_t *req);
void
tw_xwm_data_transfer_start_read(struct tw_xwm_data_transfer *transfer);

void
tw_xwm_data_transfer_read_chunk(struct tw_xwm_data_transfer *transfer);

int
tw_xwm_handle_selection_event(struct tw_xwm *xwm, xcb_generic_event_t *ge);

void
tw_xwm_data_source_init(struct tw_xwm_data_source *source,
                        struct tw_xwm_selection *selection);
void
tw_xwm_data_source_reset(struct tw_xwm_data_source *source);

bool
tw_xwm_data_source_get_targets(struct tw_xwm_data_source *source,
                               struct tw_xwm *xwm);

/* triggered by a wl_client asking for data, we would act as the client writing
 * to the fd */
void
tw_xwm_data_source_get_data(struct tw_xwm_data_source *source);

void
tw_xwm_selection_send_notify(struct tw_xwm *xwm,
                             xcb_selection_request_event_t *req, bool set);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
