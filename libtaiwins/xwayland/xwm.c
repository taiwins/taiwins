/*
 * xwm.c - taiwins xwayland xwm
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* wayland headers */
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/compositor.h>

#include <taiwins/xwayland.h>

/** x headers */
#include <wayland-util.h>
#include <xcb/composite.h>
#include <xcb/render.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xproto.h>

#include "internal.h"

#define XCB_EVENT_TYPE_MASK (0x7f)

/******************************************************************************
 * handlers
 *****************************************************************************/

/* we are creating a xcb window here, which in turns causes xwayland to create
 * a wl_surface request.
 */
static void
handle_xwm_create_surface(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	struct tw_xsurface *surface = NULL;
	xcb_create_notify_event_t *ev = (xcb_create_notify_event_t *)ge;

	if (ev->window == xwm->window) //or DND or selection window
		return;
	tw_logl("Received CreateNotify:%d for %d",
	        XCB_CREATE_NOTIFY, ev->window);
	surface = tw_xsurface_create(xwm, ev->window,
	                             ev->x, ev->y,
	                             ev->width, ev->height,
	                             ev->override_redirect);
	if (surface)
		wl_list_insert(xwm->surfaces.prev, &surface->link);
}

static void
handle_xwm_destroy_surface(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t *)ge;
	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);

	if (!surface)
		return;
	tw_logl("Received DestroyNotify:%d for %d",
	        XCB_DESTROY_NOTIFY, ev->window);
	wl_list_remove(&surface->link);
	tw_xsurface_destroy(surface);
}

static void
handle_xwm_map_request(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_map_request_event_t *ev = (xcb_map_request_event_t *)ge;
	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);

	if (!surface)
		return;
	tw_logl("Recived MapRequest:%d from xcb_window@d",
	        XCB_MAP_REQUEST, ev->window);
	// it is likely that we do not have a wl_surface yet
	tw_xsurface_map_requested(surface);
}

static void
handle_xwm_map_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_map_notify_event_t *ev = (xcb_map_notify_event_t *)ge;
	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);

	if (surface)
		tw_logl("Recived MapNotify:%d from xcb_window@%d",
		        XCB_MAP_NOTIFY, ev->window);
}

static void
handle_xwm_unmap_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)ge;
	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);

	if (!surface)
		return;
	tw_logl("Recived UnmapNotify:%d from xcb_window",
	        XCB_UNMAP_NOTIFY, ev->window);
	tw_xsurface_unmap_requested(surface);
}

/*
 * xserver/xwayland send this event to us(root window) whenever other clients
 * initiates a
 */
static void
handle_xwm_configure_request(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_configure_request_event_t *ev =
		(xcb_configure_request_event_t *)ge;

	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);
	uint32_t mask = ev->value_mask;
	uint16_t geo_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

	if (!surface || !surface->surface || ((geo_mask & mask) == 0))
		return;
	//TODO, we probably need to send a desktop request.
}

/*
 * xserver/xwayland send this event to us(root window) whenever the configure
 * request made by other clients actually completes.
 */
static void
handle_xwm_configure_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	//xcb_configure_notify_event_t *ev = (xcb_configure_notify_event_t *)ge;
        //we would need to care if the geometry is different than what we
        //expect.
}

static void
handle_xwm_client_msg(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_client_message_event_t *ev = (xcb_client_message_event_t *)ge;

	struct tw_xsurface *surface =
		tw_xsurface_from_id(xwm, ev->window);
	if (!surface)
		return;
	tw_logl("Received ClientMessage:%d from client@%d",
	        XCB_CLIENT_MESSAGE, ev->window);
	tw_xsurface_read_client_msg(surface, ev);
}

static void
handle_xwm_property_notify(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)ge;
	struct tw_xsurface *surface = tw_xsurface_from_id(xwm, ev->window);

	if (!surface)
		return;
	tw_logl("Recived PropertyNotify:%d from xcb_window@%d",
	        XCB_PROPERTY_NOTIFY, ev->window);
	tw_xsurface_read_property(surface, ev->atom);
}

static void
handle_xwm_focus_in(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
	xcb_focus_in_event_t *ev = (xcb_focus_in_event_t *)ge;

	if (ev->mode == XCB_NOTIFY_MODE_GRAB ||
	    ev->mode == XCB_NOTIFY_MODE_UNGRAB)
		return;
	tw_logl("Recived FocusIn:%d from xcb_window@d",
	        XCB_FOCUS_IN, ev->event);
	//DO NOT let X client change the focus behind the compositor's back
	//TODO: resolve problems for APP rely need focus like steam. Refer to
	//https://github.com/swaywm/wlroots/blob/master/xwayland/xwm.c#L1332
	if (!xwm->focus_window || ev->event != xwm->focus_window->id)
		tw_xsurface_set_focus(xwm->focus_window, xwm);
}

static void
handle_xwm_xcb_error(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
#if _TW_HAS_XCB_ERRORS
	xcb_value_error_t *ev = (xcb_value_error_t *)ge;
	const char *major_name, *minor_name, *extension, *error_name;

	major_name =  xcb_errors_get_name_for_major_code(xwm->errors_context,
	                                                 ev->major_opcode);
	if (!major_name) {
		tw_logl_level(TW_LOG_WARN, "XCB has error, "
		              "but without major code");
		goto log_raw;
	}
	minor_name = xcb_errors_get_name_for_minor_code(xwm->errors_context,
	                                                ev->major_opcode,
	                                                ev->minor_opcode);
	error_name = xcb_errors_get_name_for_error(xwm->errors_context,
	                                           ev->error_code, &extension);
	if (!error_name) {
		tw_logl_level(TW_LOG_WARN, "XCB has error, but could not "
		              "get error code");
		goto log_raw;
	}
	tw_logl_level(TW_LOG_WARN, "xcb error: op %s (%s), code %s (%s), "
	              "sequence %u, value %u",
	              major_name, minor_name ? minor_name : "no minor",
	              error_name, extension ? extension : "no extension",
	              ev->sequence, ev->bad_value);

log_raw:
	tw_logl_level(TW_LOG_WARN, "xcb error: op %u :%u, "
	              "code %u, sequence %u, value %u",
	              ev->major_opcode, ev->minor_opcode, ev->error_code,
	              ev->sequence, ev->bad_value);
#endif
}

static void
handle_xwm_unhandled_event(struct tw_xwm *xwm, xcb_generic_event_t *ge)
{
#if _TW_HAS_XCB_ERRORS
	const char *extension;
	const char *event_name =
		xcb_errors_get_name_for_xcb_event(xwm->errors_context, ge,
		                                  &extension);
	if (!event_name)
		tw_logl_level(TW_LOG_WARN, "noname for unhandled XCB event:%u",
		              ge->response_type);

	else
		tw_logl_level(TW_LOG_WARN, "unhandled XCB event %s:%u",
		              event_name, ge->response_type);
#else
	tw_logl_level(TW_LOG_WARN, "unhaneld XCB event %u", ge->response_type);
#endif
}

/******************************************************************************
 * XWM logic
 *****************************************************************************/

static inline int
_handle_x11_events(struct tw_xwm *xwm, xcb_generic_event_t *event)
{
	int count = 0;
	assert(event);
	do {
		count++;
		// handle selection event?
		switch (event->response_type & XCB_EVENT_TYPE_MASK) {
		case XCB_CREATE_NOTIFY:
			handle_xwm_create_surface(xwm, event);
			break;
		case XCB_DESTROY_NOTIFY:
			handle_xwm_destroy_surface(xwm, event);
			break;
		case XCB_CONFIGURE_REQUEST:
			handle_xwm_configure_request(xwm, event);
			break;
		case XCB_CONFIGURE_NOTIFY:
			handle_xwm_configure_notify(xwm, event);
			break;
		case XCB_MAP_REQUEST:
			handle_xwm_map_request(xwm, event);
			break;
		case XCB_MAP_NOTIFY:
			handle_xwm_map_notify(xwm, event);
			break;
		case XCB_UNMAP_NOTIFY:
			handle_xwm_unmap_notify(xwm, event);
			break;
		case XCB_PROPERTY_NOTIFY:
			handle_xwm_property_notify(xwm, event);
			break;
		case XCB_CLIENT_MESSAGE:
			handle_xwm_client_msg(xwm, event);
			break;
		case XCB_FOCUS_IN:
			handle_xwm_focus_in(xwm, event);
			break;
		case 0:
			handle_xwm_xcb_error(xwm, event);
			break;
		default:
			handle_xwm_unhandled_event(xwm, event);
			break;
		}
		free(event);
	} while ((event = xcb_poll_for_event(xwm->xcb_conn)));
	if (count)
		xcb_flush(xwm->xcb_conn);
	return count;
}

static int
handle_x11_events(int fd, uint32_t mask, void *data)
{
	xcb_generic_event_t *event = NULL;
	struct tw_xwm *xwm = data;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR))
		//TODO destroy
		return 0;
	if ((event = xcb_poll_for_event(xwm->xcb_conn)))
		return _handle_x11_events(xwm, event);
	else
		return 0;
}

static void
destroy_xwm(struct tw_xwm *xwm)
{
	struct tw_xsurface *surface, *tmp;

	if (xwm->x11_event) {
		wl_event_source_remove(xwm->x11_event);
		xwm->x11_event = NULL;
	}
	wl_list_for_each_safe(surface, tmp, &xwm->surfaces, link)
		tw_xsurface_destroy(surface);

	if (xwm->colormap)
		xcb_free_colormap(xwm->xcb_conn, xwm->colormap);
#if _TW_HAS_XCB_ERRORS
	if (xwm->errors_context)
		xcb_errors_context_free(xwm->errors_context);
#endif
	if (xwm->xcb_conn)
		xcb_disconnect(xwm->xcb_conn);
	free(xwm);
}

static bool
collect_xwm_atoms(struct tw_xwm *xwm)
{
	struct {
		const char *name;
		xcb_atom_t *atom;
		xcb_intern_atom_cookie_t cookie;
	} atoms[] = {
		{ "WM_PROTOCOLS",	&xwm->atoms.wm_protocols },
		{ "WM_NORMAL_HINTS",	&xwm->atoms.wm_normal_hints },
		{ "WM_TAKE_FOCUS",	&xwm->atoms.wm_take_focus },
		{ "WM_DELETE_WINDOW",	&xwm->atoms.wm_delete_window },
		{ "WM_STATE",		&xwm->atoms.wm_state },
		{ "WM_S0",		&xwm->atoms.wm_s0 },
		{ "WM_CLIENT_MACHINE",	&xwm->atoms.wm_client_machine },
		{ "_NET_WM_CM_S0",	&xwm->atoms.net_wm_cm_s0 },
		{ "_NET_WM_NAME",	&xwm->atoms.net_wm_name },
		{ "_NET_WM_PID",	&xwm->atoms.net_wm_pid },
		{ "_NET_WM_ICON",	&xwm->atoms.net_wm_icon },
		{ "_NET_WM_STATE",	&xwm->atoms.net_wm_state },
		{ "_NET_WM_STATE_FOCUSED",
		  &xwm->atoms.net_wm_state_focused },
		{ "_NET_WM_STATE_MAXIMIZED_VERT",
		  &xwm->atoms.net_wm_state_maximized_vert },
		{ "_NET_WM_STATE_MAXIMIZED_HORZ",
		  &xwm->atoms.net_wm_state_maximized_horz },
		{ "_NET_WM_STATE_FULLSCREEN",
		  &xwm->atoms.net_wm_state_fullscreen },
		{ "_NET_WM_STATE_HIDDEN",
		  &xwm->atoms.net_wm_state_hidden },
		{ "_NET_WM_USER_TIME", &xwm->atoms.net_wm_user_time },
		{ "_NET_WM_ICON_NAME", &xwm->atoms.net_wm_icon_name },
		{ "_NET_WM_DESKTOP", &xwm->atoms.net_wm_desktop },
		{ "_NET_WM_WINDOW_TYPE", &xwm->atoms.net_wm_window_type },

		{ "_NET_WM_WINDOW_TYPE_DESKTOP",
		  &xwm->atoms.net_wm_window_type_desktop },
		{ "_NET_WM_WINDOW_TYPE_DOCK",
		  &xwm->atoms.net_wm_window_type_dock },
		{ "_NET_WM_WINDOW_TYPE_TOOLBAR",
		  &xwm->atoms.net_wm_window_type_toolbar },
		{ "_NET_WM_WINDOW_TYPE_MENU",
		  &xwm->atoms.net_wm_window_type_menu },
		{ "_NET_WM_WINDOW_TYPE_UTILITY",
		  &xwm->atoms.net_wm_window_type_utility },
		{ "_NET_WM_WINDOW_TYPE_SPLASH",
		  &xwm->atoms.net_wm_window_type_splash },
		{ "_NET_WM_WINDOW_TYPE_DIALOG",
		  &xwm->atoms.net_wm_window_type_dialog },
		{ "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
		  &xwm->atoms.net_wm_window_type_dropdown },
		{ "_NET_WM_WINDOW_TYPE_POPUP_MENU",
		  &xwm->atoms.net_wm_window_type_popup },
		{ "_NET_WM_WINDOW_TYPE_TOOLTIP",
		  &xwm->atoms.net_wm_window_type_tooltip },
		{ "_NET_WM_WINDOW_TYPE_NOTIFICATION",
		  &xwm->atoms.net_wm_window_type_notification },
		{ "_NET_WM_WINDOW_TYPE_COMBO",
		  &xwm->atoms.net_wm_window_type_combo },
		{ "_NET_WM_WINDOW_TYPE_DND",
		  &xwm->atoms.net_wm_window_type_dnd },
		{ "_NET_WM_WINDOW_TYPE_NORMAL",
		  &xwm->atoms.net_wm_window_type_normal },

		{ "_NET_WM_MOVERESIZE", &xwm->atoms.net_wm_moveresize },
		{ "_NET_SUPPORTING_WM_CHECK",
					&xwm->atoms.net_supporting_wm_check },
		{ "_NET_SUPPORTED",     &xwm->atoms.net_supported },
		{ "_NET_ACTIVE_WINDOW",     &xwm->atoms.net_active_window },
		{ "_MOTIF_WM_HINTS",	&xwm->atoms.motif_wm_hints },
		{ "CLIPBOARD",		&xwm->atoms.clipboard },
		{ "CLIPBOARD_MANAGER",	&xwm->atoms.clipboard_manager },
		{ "TARGETS",		&xwm->atoms.targets },
		{ "UTF8_STRING",	&xwm->atoms.utf8_string },
		{ "_WL_SELECTION",	&xwm->atoms.wl_selection },
		{ "INCR",		&xwm->atoms.incr },
		{ "TIMESTAMP",		&xwm->atoms.timestamp },
		{ "MULTIPLE",		&xwm->atoms.multiple },
		{ "UTF8_STRING"	,	&xwm->atoms.utf8_string },
		{ "COMPOUND_TEXT",	&xwm->atoms.compound_text },
		{ "TEXT",		&xwm->atoms.text },
		{ "STRING",		&xwm->atoms.string },
		{ "WINDOW",		&xwm->atoms.window },
		{ "text/plain;charset=utf-8",	&xwm->atoms.text_plain_utf8 },
		{ "text/plain",		&xwm->atoms.text_plain },
		{ "XdndSelection",	&xwm->atoms.xdnd_selection },
		{ "XdndAware",		&xwm->atoms.xdnd_aware },
		{ "XdndEnter",		&xwm->atoms.xdnd_enter },
		{ "XdndLeave",		&xwm->atoms.xdnd_leave },
		{ "XdndDrop",		&xwm->atoms.xdnd_drop },
		{ "XdndStatus",		&xwm->atoms.xdnd_status },
		{ "XdndFinished",	&xwm->atoms.xdnd_finished },
		{ "XdndTypeList",	&xwm->atoms.xdnd_type_list },
		{ "XdndActionCopy",	&xwm->atoms.xdnd_action_copy },
		{ "_XWAYLAND_ALLOW_COMMITS",	&xwm->atoms.allow_commits },
		{ "WL_SURFACE_ID",	&xwm->atoms.wl_surface_id }
	};
	const int n_atoms = sizeof(atoms) / sizeof(atoms[0]);
	xcb_intern_atom_reply_t *reply;
	xcb_generic_error_t *error;

	for (int i = 0; i < n_atoms; i++)
		atoms[i].cookie = xcb_intern_atom(xwm->xcb_conn, 0,
		                                  strlen(atoms[i].name),
		                                  atoms[i].name);
	for (int i = 0; i < n_atoms; i++) {
		reply = xcb_intern_atom_reply(xwm->xcb_conn, atoms[i].cookie,
		                              &error);
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

static bool
collect_xwm_xfixes(struct tw_xwm *xwm)
{
	xcb_xfixes_query_version_cookie_t xfixes_cookie;
	xcb_xfixes_query_version_reply_t *xfixes_reply;

	xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_xfixes_id);
	xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_composite_id);

	xwm->xfixes = xcb_get_extension_data(xwm->xcb_conn, &xcb_xfixes_id);
	if (!xwm->xfixes || !xwm->xfixes->present) {
		tw_logl_level(TW_LOG_WARN, "xfixes not available\n");
		return false;
	}
	xfixes_cookie = xcb_xfixes_query_version(xwm->xcb_conn,
	                                         XCB_XFIXES_MAJOR_VERSION,
	                                         XCB_XFIXES_MINOR_VERSION);
	xfixes_reply = xcb_xfixes_query_version_reply(xwm->xcb_conn,
	                                              xfixes_cookie, NULL);
	tw_logl("xfixes version: %d.%d\n",
	        xfixes_reply->major_version, xfixes_reply->minor_version);

	free(xfixes_reply);

	return true;
}

static bool
collect_render_formats(struct tw_xwm *xwm)
{
	xcb_render_query_pict_formats_reply_t *formats_reply;
	xcb_render_query_pict_formats_cookie_t formats_cookie;
	xcb_render_pictforminfo_t *formats;
	bool found_rgba = false;

	formats_cookie = xcb_render_query_pict_formats(xwm->xcb_conn);
	formats_reply = xcb_render_query_pict_formats_reply(xwm->xcb_conn,
	                                                    formats_cookie, 0);
	if (formats_reply == NULL)
		return false;
	formats = xcb_render_query_pict_formats_formats(formats_reply);
	//picking a RGBA format
	for (unsigned i = 0; i < formats_reply->num_formats; i++) {
		if (formats[i].type == XCB_RENDER_PICT_TYPE_DIRECT &&
		    formats[i].direct.red_mask == 0xff &&
		    formats[i].direct.red_shift == 16 &&
		    formats[i].depth == 32 &&
		    formats[i].direct.alpha_mask == 0xff &&
		    formats[i].direct.alpha_shift == 24) {
			xwm->format_rgba = formats[i].id;
			found_rgba = true;
			break;
		}
	}
	free(formats_reply);
	return found_rgba;
}

static bool
collect_visual_colormap(struct tw_xwm *xwm)
{
	xcb_depth_iterator_t d_iter;
	xcb_visualtype_iterator_t vt_iter;
	xcb_visualtype_t *visualtype;

	d_iter = xcb_screen_allowed_depths_iterator(xwm->screen);
	visualtype = NULL;

	while(d_iter.rem > 0) {
		if (d_iter.data->depth == 32) {
			vt_iter = xcb_depth_visuals_iterator(d_iter.data);
			visualtype = vt_iter.data;
			break;
		}
		xcb_depth_next(&d_iter);
	}

	if (visualtype == NULL) {
		tw_logl_level(TW_LOG_WARN, "Failed to pick a visual id");
		return false;
	}
	xwm->visual_id = visualtype->visual_id;
	xwm->colormap = xcb_generate_id(xwm->xcb_conn);
	xcb_create_colormap(xwm->xcb_conn, XCB_COLORMAP_ALLOC_NONE,
	                    xwm->colormap, xwm->screen->root, xwm->visual_id);
	return true;
}

static bool
change_window_attributes(struct tw_xwm *xwm)
{
	uint32_t values = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_atom_t supported[] = {
		xwm->atoms.net_wm_state,
		xwm->atoms.net_active_window,
		xwm->atoms.net_wm_moveresize,
		xwm->atoms.net_wm_state_focused,
		xwm->atoms.net_wm_state_fullscreen,
		xwm->atoms.net_wm_state_hidden,
		xwm->atoms.net_wm_state_maximized_horz,
		xwm->atoms.net_wm_state_maximized_vert,
	};

	xcb_change_window_attributes(xwm->xcb_conn, xwm->screen->root,
	                             XCB_CW_EVENT_MASK, &values);
	xcb_composite_redirect_subwindows(xwm->xcb_conn, xwm->screen->root,
	                                  XCB_COMPOSITE_REDIRECT_MANUAL);

	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE,
	                    xwm->screen->root, xwm->atoms.net_supported,
	                    XCB_ATOM_ATOM, 32,
	                    sizeof(supported)/sizeof(*supported),
	                    supported);
	tw_xwm_set_net_active_window(xwm, XCB_WINDOW_NONE);
	return true;
}

static void
create_xwm_wm_window(struct tw_xwm *xwm)
{
	static const char name[] = "taiwins wm";

	xwm->window = xcb_generate_id(xwm->xcb_conn);

	xcb_create_window(xwm->xcb_conn,
	                  XCB_COPY_FROM_PARENT,
	                  xwm->window,
	                  xwm->screen->root,
	                  0, 0, 10, 10,
	                  0,
	                  XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  xwm->screen->root_visual,
	                  0, NULL);
	xcb_change_property(xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    xwm->window,
	                    xwm->atoms.net_wm_name,
	                    xwm->atoms.utf8_string,
	                    8, // format
	                    strlen(name), name);
	xcb_change_property(xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    xwm->screen->root,
	                    xwm->atoms.net_supporting_wm_check,
	                    XCB_ATOM_WINDOW,
	                    32, // format
	                    1, &xwm->window);

	xcb_change_property(xwm->xcb_conn,
	                    XCB_PROP_MODE_REPLACE,
	                    xwm->window,
	                    xwm->atoms.net_supporting_wm_check,
	                    XCB_ATOM_WINDOW,
	                    32, // format
	                    1, &xwm->window);

	xcb_set_selection_owner(xwm->xcb_conn,
	                        xwm->window,
	                        xwm->atoms.wm_s0,
	                        XCB_CURRENT_TIME);

	xcb_set_selection_owner(xwm->xcb_conn,
	                        xwm->window,
	                        xwm->atoms.net_wm_cm_s0,
	                        XCB_CURRENT_TIME);

}

static void
notify_xwm_wl_surface_created(struct wl_listener *listener, void *data)
{
	struct tw_xsurface *xsurface;
	struct tw_surface *tw_surface = data;
	uint32_t id = wl_resource_get_id(tw_surface->resource);
	struct tw_xwm *xwm =
		wl_container_of(listener, xwm, listeners.wl_surface_create);

	wl_list_for_each(xsurface, &xwm->surfaces, link)
		if (xsurface->surface_id == id)
			tw_xsurface_map_tw_surface(xsurface, tw_surface);
}

static void
notify_xwm_server_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xwm *xwm =
		wl_container_of(listener, xwm, listeners.server_destroy);
	destroy_xwm(xwm);
}

/* we are basically writing a small xwindow manager here, it should be no
 * different than predecessors like dwm, i3.
 *
 * 1) We are gaining a connection to the server end of the socket.
 * 2) Setting up server resources and listening on the wmfd for events.
 * 3) processing the events and sending the input events to xserver
 */
WL_EXPORT struct tw_xwm *
tw_xserver_create_xwindow_manager(struct tw_xserver *server,
                                  struct tw_desktop_manager *desktop_manager,
                                  struct tw_compositor *compositor)
{
	xcb_screen_iterator_t screen_iterator;
	struct tw_xwm *xwm = NULL;
	struct wl_display *display = server->wl_display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	if (!(xwm = calloc(1, sizeof(*xwm)))) {
		return NULL;
	}
	xwm->server = server;
	xwm->manager = desktop_manager;
	wl_list_init(&xwm->surfaces);

	if (!(xwm->xcb_conn = xcb_connect_to_fd(server->wms[0], NULL)))
		goto err;
	if (xcb_connection_has_error(xwm->xcb_conn))
		goto err;

#if _TW_HAS_XCB_ERRORS
	if (xcb_errors_context_new(xwm->xcb_conn, &xwm->errors_context)) {
		tw_logl_level(TW_LOG_ERRO, "Could not allocate error context");
		destroy_xwm(xwm);
		return NULL;
	}
#endif

	screen_iterator =
		xcb_setup_roots_iterator(xcb_get_setup(xwm->xcb_conn));
	xwm->screen = screen_iterator.data;

        if (!(xwm->x11_event = wl_event_loop_add_fd(loop, server->wms[0],
	                                        WL_EVENT_READABLE,
	                                        handle_x11_events,
	                                        xwm)))
		goto err;

        //TODO need new wl_surface event for unmapped surface, this makes us
        //request wl_compositor object
        if (!collect_xwm_atoms(xwm))
	        goto err;
        if (!collect_xwm_xfixes(xwm))
	        goto err;
        if (!collect_render_formats(xwm))
	        goto err;
        if (!collect_visual_colormap(xwm))
	        goto err;
        if (!change_window_attributes(xwm))
	        goto err;
        //TODO create cursor,

	create_xwm_wm_window(xwm);

	tw_signal_setup_listener(&server->signals.destroy,
	                         &xwm->listeners.server_destroy,
	                         notify_xwm_server_destroy);
	tw_signal_setup_listener(&compositor->surface_created,
	                         &xwm->listeners.wl_surface_create,
	                         notify_xwm_wl_surface_created);

	wl_event_source_check(xwm->x11_event);
        xcb_flush(xwm->xcb_conn);
        return xwm;
err:
	destroy_xwm(xwm);
	return NULL;
}
