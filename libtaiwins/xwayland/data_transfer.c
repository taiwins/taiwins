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
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <taiwins/objects/logger.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <ctypes/helpers.h>

#include "internal.h"

#define INCR_CHUNK_SIZE (64 * 1024)

static inline int
xwm_data_transfer_get_available(struct tw_xwm_data_transfer *transfer)
{
	return MAX(INCR_CHUNK_SIZE - transfer->cached, 0);
}

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
xwm_data_transfer_fini_write(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = transfer->selection->xwm;

	xcb_delete_property(xwm->xcb_conn, selection->window,
	                    xwm->atoms.wl_selection);
	xcb_flush(xwm->xcb_conn);
	transfer->property_offset = 0;
}

static int
xwm_data_transfer_write(int fd, uint32_t mask, void *data)
{
	struct tw_xwm_data_transfer *transfer = data;

	char *property = xcb_get_property_value(transfer->property_reply);
	int remains = xcb_get_property_value_length(transfer->property_reply) -
		transfer->property_offset;
	ssize_t len = write(fd, property + transfer->property_offset, remains);
	transfer->property_offset += MAX(len, 0);

	if (len == -1)
		tw_log_level(TW_LOG_WARN, "write error in writing "
		             "wl_data_offer target fd %d", fd);
	if (len == -1 || len == remains) {
		xwm_data_transfer_destroy_reply(transfer);
		xwm_data_transfer_remove_source(transfer);
		xwm_data_transfer_fini_write(transfer);

		if (!transfer->in_chunk)
			xwm_data_transfer_close_fd(transfer);
	}
	return 1;
}

static inline void
handle_write_property(struct tw_xwm_data_transfer *transfer,
                      xcb_get_property_reply_t *reply)
{
	uint32_t mask = WL_EVENT_WRITABLE;
	transfer->property_offset = 0;
	transfer->property_reply = reply;

	//not done yet
	if (xwm_data_transfer_write(transfer->fd, mask, transfer) != 0)
		xwm_data_transfer_add_fd(transfer, transfer->fd, mask,
		                         xwm_data_transfer_write);
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

void
tw_xwm_data_transfer_start_write(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = selection->xwm;
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
		                 0, //delete
		                 selection->window,
		                 xwm->atoms.wl_selection,
		                 XCB_GET_PROPERTY_TYPE_ANY,
		                 0, //offset
		                 0x1fffffff // length
			);
	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);

	if (reply->type == xwm->atoms.incr) {
		//for handling chunks, the spec stats: The selection requestor
		//starts the transfer process by deleting the (type==INCR)
		//property forming the reply to the selection, the delete was
		//done
		transfer->in_chunk = true;
		xcb_delete_property(xwm->xcb_conn, selection->window,
		                    xwm->atoms.wl_selection);
		free(reply);
	} else {
		transfer->in_chunk = false;
		handle_write_property(transfer, reply);
	}
}

void
tw_xwm_data_transfer_continue_write(struct tw_xwm_data_transfer *transfer)
{
	struct tw_xwm_selection *selection = transfer->selection;
	struct tw_xwm *xwm = selection->xwm;
	//getting properties, we are deleting ourselves
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
		                 0, //delete
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
	if (xcb_get_property_value_length(reply) > 0) {
		handle_write_property(transfer, reply);
	} else {
		tw_logl_level(TW_LOG_DBUG, "transfer complete");
		xwm_data_transfer_close_fd(transfer);
		xwm_data_transfer_fini_write(transfer);
		free(reply);
	}
}

/******************************************************************************
 * read transfer
 *****************************************************************************/

static inline void
xwm_data_transfer_fini_read(struct tw_xwm_data_transfer *transfer)
{
	xwm_data_transfer_remove_source(transfer);
	xwm_data_transfer_close_fd(transfer);

	free(transfer->data);
	transfer->in_chunk = false;
	transfer->property_set = false;
	transfer->data = NULL;
	transfer->fd = -1;
	transfer->cached = 0;
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
xwm_data_transfer_read_begin_chunk(struct tw_xwm_data_transfer *transfer)
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
	//remove the source for now and wait for
	xwm_data_transfer_remove_source(transfer);
	tw_xwm_selection_send_notify(xwm, &transfer->req, true);
}

static inline void
xwm_data_transfer_read_end_chunk(struct tw_xwm_data_transfer *transfer)
{
	xcb_change_property(transfer->selection->xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    transfer->req.requestor,
	                    transfer->req.property,
	                    transfer->req.target,
	                    32,
	                    0, NULL);
}

static int
xwm_data_transfer_read_chunk(int fd, uint32_t mask, void *data)
{
	struct tw_xwm_data_transfer *transfer = data;
	unsigned avail = xwm_data_transfer_get_available(transfer);
	int len = 0;

	if (transfer->property_set) {
		tw_logl_level(TW_LOG_WARN, "property not yet deleted");
		return 1;
	}

	//read if we can
	if (avail) {
		len = read(fd, transfer->data+transfer->cached, avail);
		transfer->cached += len > 0 ? len : 0;
		avail = xwm_data_transfer_get_available(transfer);
	}
	if ((len <= 0 && transfer->cached) || avail == 0) {
		//we either read enough or no more data to read, either way we
		//send what is cached
		xwm_data_transfer_read_flush(transfer);
		xwm_data_transfer_remove_source(transfer);
		tw_xwm_selection_send_notify(transfer->selection->xwm,
		                             &transfer->req, true);
		transfer->done = len == 0;
	} else if (len <= 0 && !transfer->cached) {
		//edge case: nothing to send and nothing to read
		xwm_data_transfer_read_end_chunk(transfer);
		xwm_data_transfer_fini_read(transfer);
	} else {
		//continue accumulating
		tw_logl_level(TW_LOG_DBUG, "continue accumulate data_transfer");
	}
	return 1;
}

static int
xwm_data_transfer_read(int fd, uint32_t mask, void *data)
{
	struct tw_xwm_data_transfer *transfer = data;
	int available = xwm_data_transfer_get_available(transfer);
	ssize_t len = 0;

	//we begin by read as much as we can
	len = read(fd, transfer->data+transfer->cached, available);
	transfer->cached += len > 0 ? len : 0;
	available = xwm_data_transfer_get_available(transfer);

	if (len == -1) {
		tw_logl_level(TW_LOG_WARN, "read error from data source");
		tw_xwm_selection_send_notify(transfer->selection->xwm,
		                             &transfer->req, false);
		xwm_data_transfer_fini_read(transfer);
	} else if (len == 0) {
		xwm_data_transfer_read_flush(transfer);
		tw_xwm_selection_send_notify(transfer->selection->xwm,
		                             &transfer->req, true);
		xwm_data_transfer_fini_read(transfer);
	} else if (available == 0) {
		//need to write in chunks
		xwm_data_transfer_read_begin_chunk(transfer);
		return 0;
	} else {
		//continue accumulating
		tw_logl_level(TW_LOG_DBUG, "continue accumulate data_transfer");
		return 1;
	}
	return 0;
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

void
tw_xwm_data_transfer_start_read(struct tw_xwm_data_transfer *transfer)
{
	if (transfer->in_chunk)
		xwm_data_transfer_add_fd(transfer, transfer->fd,
		                         WL_EVENT_READABLE,
		                         xwm_data_transfer_read_chunk);
	else
		xwm_data_transfer_add_fd(transfer, transfer->fd,
		                         WL_EVENT_READABLE,
		                         xwm_data_transfer_read);
}

void
tw_xwm_data_transfer_continue_read(struct tw_xwm_data_transfer *transfer)
{
	if (transfer->done) {
		xwm_data_transfer_read_end_chunk(transfer);
		xwm_data_transfer_fini_read(transfer);
	} else
		tw_xwm_data_transfer_start_read(transfer);
}
