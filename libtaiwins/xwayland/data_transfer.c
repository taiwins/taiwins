/*
 * data_transfer.c - taiwins xwayland selection data_source implementation
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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "selection.h"
#include "internal.h"
#include "taiwins/objects/logger.h"

#define INCR_CHUNK_SIZE (64 * 1024)

static inline void
xwm_data_transfer_add_fd(struct tw_xwm_data_transfer *transfer, int fd,
                         uint32_t mask, wl_event_loop_fd_func_t func)
{
	struct tw_xwm *xwm = transfer->selection->xwm;
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xwm->server->wl_display);
	transfer->event = wl_event_loop_add_fd(loop, fd, mask, func, transfer);
}

static inline void
xwm_data_transfer_destroy_reply(struct tw_xwm_data_transfer *transfer)
{
	free(transfer->property_reply);
	transfer->property_reply = NULL;
}

static inline void
xwm_data_transfer_remove_source(struct tw_xwm_data_transfer *transfer)
{
	if (transfer->event) {
		wl_event_source_remove(transfer->event);
		transfer->event = NULL;
	}
}

static inline void
xwm_data_transfer_close_fd(struct tw_xwm_data_transfer *transfer)
{
	if (transfer->fd >= 0) {
		close(transfer->fd);
		transfer->fd = -1;
	}
}

/******************************************************************************
 * write transfer
 *****************************************************************************/

static inline void
xwm_data_transfer_fini_chunk(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = transfer->selection->xwm;

	xcb_delete_property(xwm->xcb_conn, selection->window,
	                    xwm->atoms.wl_selection);
	xcb_flush(xwm->xcb_conn);
	transfer->property_offset = 0;
}

static int
handle_write_unblock(int fd, uint32_t mask, void *data)
{
	int ret = 1; //mark for recheck
	struct tw_xwm_data_transfer *transfer = data;

	char *property = xcb_get_property_value(transfer->property_reply);
	int remains = xcb_get_property_value_length(transfer->property_reply) -
		transfer->property_offset;
	ssize_t len = write(fd, property + transfer->property_offset, remains);

	if (len == -1) {
		xwm_data_transfer_destroy_reply(transfer);
		xwm_data_transfer_remove_source(transfer);
		xwm_data_transfer_close_fd(transfer);
		tw_log_level(TW_LOG_WARN, "write error in writing "
		             "wl_data_source");
		return 0;
	}
	//advance the offset
	transfer->property_offset += len;
	if (len == remains) { //done writing
		xwm_data_transfer_destroy_reply(transfer);
		xwm_data_transfer_remove_source(transfer);
		//if we are writing the chunk, just close this one
		if (transfer->in_chunk) {
			tw_logl_level(TW_LOG_DBUG, "done writing chunk, "
			              "to next chunk");
			xwm_data_transfer_fini_chunk(transfer);
		} else {
			tw_logl_level(TW_LOG_DBUG, "transfer complete");
			xwm_data_transfer_close_fd(transfer);
			ret = 0;
		}
	}
	return ret;
}

void
tw_xwm_data_transfer_init_write(struct tw_xwm_data_transfer *transfer,
                                struct tw_xwm_selection *selection,
                                int fd)
{
	transfer->fd = fd;
	transfer->in_chunk = false;
	transfer->selection = selection;
	fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
}

static inline void
handle_write_property(struct tw_xwm_data_transfer *transfer,
                      xcb_get_property_reply_t *reply)
{
	uint32_t mask = WL_EVENT_WRITABLE;
	transfer->property_offset = 0;
	transfer->property_reply = reply;

	//not done yet
	if (handle_write_unblock(transfer->fd, mask, transfer) != 0)
		xwm_data_transfer_add_fd(transfer, transfer->fd, mask,
		                         handle_write_unblock);
}

/* ref:
https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#incr_properties
*/
static inline void
handle_write_start_chunks(struct tw_xwm_data_transfer *transfer,
                          xcb_get_property_reply_t *reply)
{
	//for handling chunks, the spec stats: The selection requestor starts
	//the transfer process by deleting the (type==INCR) property forming
	//the reply to the selection
	free(reply);
}

void
tw_xwm_data_transfer_start_write(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = selection->xwm;
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
		                 1, //delete
		                 selection->window,
		                 xwm->atoms.wl_selection,
		                 XCB_GET_PROPERTY_TYPE_ANY,
		                 0, //offset
		                 0x1fffffff // length
			);
	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	//if we
	if (reply->type == xwm->atoms.incr) {
		transfer->in_chunk = true;
		handle_write_start_chunks(transfer, reply);
	} else {
		transfer->in_chunk = false;
		handle_write_property(transfer, reply);
	}
}

//we are here because we delete the property and the owner wrote the property
void
tw_xwm_data_transfer_write_chunk(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = selection->xwm;
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
		                 1, //delete
		                 selection->window,
		                 xwm->atoms.wl_selection,
		                 XCB_GET_PROPERTY_TYPE_ANY,
		                 0, //offset
		                 0x1fffffff // length
			);
	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (!reply) {
		tw_logl_level(TW_LOG_WARN, "Could not get reply");
		return;
	}
	if (xcb_get_property_value_length(reply) > 0)
		handle_write_property(transfer, reply);
	else {
		tw_logl_level(TW_LOG_DBUG, "transfer complete");
		xwm_data_transfer_close_fd(transfer);
		free(reply);
	}
}

/******************************************************************************
 * read transfer
 *****************************************************************************/

static void
xwm_data_transfer_fini_read(struct tw_xwm_data_transfer *transfer)
{
	free(transfer->data);
	transfer->in_chunk = false;
	transfer->data = NULL;
	transfer->fd = -1;
	transfer->cached = 0;
}

int
tw_xwm_data_transfer_init_read(struct tw_xwm_data_transfer *transfer,
                               struct tw_xwm_selection *selection,
                               xcb_selection_request_event_t *req)
{
	int p[2] = {-1, -1};
	if (pipe(p) == -1)
		return false;
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFL, O_NONBLOCK);

	tw_xwm_data_transfer_init_write(transfer, selection, p[0]);
	transfer->cached = 0;
	transfer->req = *req;
	transfer->data = malloc(INCR_CHUNK_SIZE);
	if (!transfer->data) {
		close(p[0]);
		close(p[1]);
		transfer->fd = -1;
		return -1;
	}
	return p[1];
}

static void
xwm_data_transfer_read_flush(struct tw_xwm_data_transfer *transfer)
{
	xcb_change_property(transfer->selection->xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    transfer->req.requestor,
	                    transfer->req.property,
	                    transfer->req.target,
	                    8, //format
	                    transfer->cached, //size
	                    transfer->data);
	xcb_flush(transfer->selection->xwm->xcb_conn);
	transfer->cached = 0;
	transfer->property_set = true;
}

static inline void
xwm_data_transfer_begin_read_chunk(struct tw_xwm_data_transfer *transfer)
{
	size_t incr_chunk_size = INCR_CHUNK_SIZE;
	struct tw_xwm *xwm = transfer->selection->xwm;
	transfer->in_chunk = true;
	xcb_change_property(transfer->selection->xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    transfer->req.requestor,
	                    transfer->req.property,
	                    xwm->atoms.incr,
	                    32, //format
	                    1,
	                    &incr_chunk_size);
}

static inline void
xwm_data_transfer_end_read_chunk(struct tw_xwm_data_transfer *transfer)
{
	xcb_change_property(transfer->selection->xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    transfer->req.requestor,
	                    transfer->req.property,
	                    transfer->req.target,
	                    32,
	                    0, NULL);
}

//now we begin to read from wl_data_source, there are a few cases to handle. If
//the data is few, we just write it to the property and we are done. Otherwise,
//we need to open-up the chunk and begin the chunk transport
static int
xwm_data_transfer_read(int fd, uint32_t mask, void *data)
{
	struct tw_xwm_data_transfer *transfer = data;
	struct tw_xwm *xwm = transfer->selection->xwm;
	int available = INCR_CHUNK_SIZE - transfer->cached;
	ssize_t len = 0;

	//we begin by read as much as we can
	available = available > 0 ? available : 0;
	len = read(fd, transfer->data + transfer->cached, available);
	transfer->cached += len > 0 ? len : 0;
	if (len == -1) {
		tw_logl_level(TW_LOG_WARN, "read error from data source");
		xwm_data_transfer_remove_source(transfer);
		xwm_data_transfer_close_fd(transfer);
		xwm_data_transfer_fini_read(transfer);
	}
	//need to avoid
	if (transfer->cached >= INCR_CHUNK_SIZE && !transfer->in_chunk) {
		xwm_data_transfer_begin_read_chunk(transfer);
		tw_xwm_selection_send_notify(xwm, &transfer->req, true);
	} else if (transfer->property_set) {
		tw_logl_level(TW_LOG_DBUG, "property not yet deleted");
		xwm_data_transfer_remove_source(transfer);
	} else if (transfer->cached) {
		//flush the buffer for now
		xwm_data_transfer_read_flush(transfer);
		xwm_data_transfer_remove_source(transfer);
	} else {
		//there is no cached, no property,
		if (transfer->in_chunk) {
			tw_logl_level(TW_LOG_DBUG, "finish writing chunks");
			xwm_data_transfer_end_read_chunk(transfer);
		} else {
			tw_logl_level(TW_LOG_DBUG, "finish writing");
			tw_xwm_selection_send_notify(xwm, &transfer->req,
			                             true);
		}
		xwm_data_transfer_remove_source(transfer);
		xwm_data_transfer_close_fd(transfer);
		xwm_data_transfer_fini_read(transfer);
	}
	return 1;
}

void
tw_xwm_data_transfer_start_read(struct tw_xwm_data_transfer *transfer)
{
	xwm_data_transfer_add_fd(transfer, transfer->fd, WL_EVENT_READABLE,
	                         xwm_data_transfer_read);
}

void
tw_xwm_data_transfer_read_chunk(struct tw_xwm_data_transfer *transfer)
{
	transfer->property_offset = false;
	if (transfer->fd >= 0)
		xwm_data_transfer_add_fd(transfer, transfer->fd,
		                         WL_EVENT_READABLE,
		                         xwm_data_transfer_read);
}
