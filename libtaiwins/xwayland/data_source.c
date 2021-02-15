/*
 * data_source.c - taiwins xwayland selection data_source implementation
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

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <wayland-server.h>
#include <taiwins/objects/data_device.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "selection.h"
#include "internal.h"
#include "taiwins/objects/logger.h"
#include "xwayland/atoms.h"

/*
 * now we recevies a wayland client as for writing the data. Then we would ask
 * our selection window to do a convert so we can trigger the
 */
static void
handle_selection_ask_for_data(struct tw_data_source *base,
                              const char *mime_asked, int fd)
{
	struct tw_xwm_data_source *source =
		wl_container_of(base, source, wl_source);
	struct tw_xwm_selection *selection = source->selection;
	struct tw_xwm *xwm = selection->xwm;
	xcb_atom_t *pmime = NULL, mime = XCB_ATOM_NONE;
	char **pmime_name = NULL;
	int i = 0;

	wl_array_for_each(pmime, &source->mime_types) {
		pmime_name = (char **)base->mimes.data + i;
		if (*pmime_name && (strcmp(*pmime_name, mime_asked) == 0)) {
			mime = *pmime;
			break;
		}
		i++;
	}

	if (mime == XCB_ATOM_NONE) {
		tw_logl_level(TW_LOG_WARN, "Failed to send X11 selection to "
		              "wayland, no supportted MIME found");
		return;
	}
	//Now convert the selection into the WL_SELECTION property under
	//selection->window
	xcb_convert_selection(xwm->xcb_conn,
	                      selection->window, //requestor
	                      selection->type, //CLIPBOARD or DND
	                      mime, // convert to target
	                      xwm->atoms.wl_selection, //property
	                      XCB_TIME_CURRENT_TIME);
	xcb_flush(xwm->xcb_conn);
	//prepare the write
	tw_xwm_data_transfer_init_write(&selection->write_transfer,
	                                selection, fd);
}

static const struct tw_data_source_impl data_source_impl = {
	.send = handle_selection_ask_for_data,
};

bool
tw_xwm_data_source_get_targets(struct tw_xwm_data_source *source,
                               struct tw_xwm *xwm)
{
	struct tw_xwm_selection *selection = source->selection;
	xcb_atom_t *value = NULL;
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
		                 1, //delete
		                 selection->window,
		                 xwm->atoms.wl_selection,
		                 XCB_GET_PROPERTY_TYPE_ANY,
		                 0,
		                 4096);
	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
        if (reply == NULL)
		return false;
	if (reply->type != XCB_ATOM_ATOM) {
		free(reply);
		return false;
	}
	value = xcb_get_property_value(reply);
	for (unsigned i = 0; i < reply->value_len; i++) {
		char *mime_type = xwm_mime_atom_to_name(xwm, value[i]);

		if (mime_type) {
			char **pmime_type =
				wl_array_add(&source->wl_source.mimes,
				             sizeof(*pmime_type));
			xcb_atom_t *patom = wl_array_add(&source->mime_types,
			                                 sizeof(*patom));
			if (patom && pmime_type) {
				*patom = value[i];
				*pmime_type = mime_type;
			} else if (pmime_type) {
				//null is okay here
				*pmime_type = NULL;
				free(mime_type);
				break;
			} else if (*patom) {
				break;
			}
		}
	}
	free(reply);
	return true;
}

void
tw_xwm_data_source_init(struct tw_xwm_data_source *source,
                        struct tw_xwm_selection *selection)
{
	source->selection = selection;

	wl_array_init(&source->mime_types);

	tw_data_source_init(&source->wl_source, NULL, &data_source_impl);
}

void
tw_xwm_data_source_reset(struct tw_xwm_data_source *source)
{
	wl_array_release(&source->mime_types);

	tw_data_source_fini(&source->wl_source);
	tw_xwm_data_source_init(source, source->selection);
}
