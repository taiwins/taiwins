#include "options.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/utils.h>

#include "internal.h"

/* check if surface has parent */
static inline bool
check_parent(struct tw_xsurface *surface, struct tw_xsurface *parent)
{
	while (surface) {
		if (parent == surface)
			return true;

		surface = surface->parent;
	}

	return false;
}

static void
send_surface_wm_msg(struct tw_xsurface *surface,
                    xcb_client_message_data_t *data, uint32_t event_mask)
{
	struct tw_xwm *xwm = surface->xwm;
	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = surface->id,
		.type = xwm->atoms.wm_protocols,
		.data = *data,
	};
	xcb_send_event(xwm->xcb_conn,
	               0, //propagate
	               surface->id,
	               event_mask, (const char *)&event);
	//TODO optional?
	xcb_flush(xwm->xcb_conn);
}

#define ICCCM_WITHDRAWN_STATE	0
#define ICCCM_NORMAL_STATE	1
#define ICCCM_ICONIC_STATE	3

static void
set_xsurface_wm_state(struct tw_xsurface *surface, int32_t state)
{
	struct tw_xwm *xwm = surface->xwm;
	uint32_t property[2] = {state, XCB_WINDOW_NONE};

	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE, surface->id,
	                    xwm->atoms.wm_state, xwm->atoms.wm_state,
	                    32, //format
	                    2, property);
}

static void
set_xsurface_net_wm_state(struct tw_xsurface *surface)
{
	struct tw_xwm *xwm = surface->xwm;
	uint32_t property[6];
	size_t i = 0;
	struct tw_desktop_surface *dsurf = &surface->dsurf;

	if (dsurf->fullscreened) {
		property[i++] = xwm->atoms.net_wm_state_fullscreen;
	} else if (dsurf->maximized) {
		property[i++] = xwm->atoms.net_wm_state_maximized_horz;
		property[i++] = xwm->atoms.net_wm_state_maximized_vert;
	} else if (dsurf->minimized) {
		property[i++] = xwm->atoms.net_wm_state_hidden;
	}

	if (surface == xwm->focus_window)
		property[i++] = xwm->atoms.net_wm_state_focused;
	if (i)
		xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE,
		                    surface->id,
		                    xwm->atoms.net_wm_state, XCB_ATOM_ATOM,
		                    32, // format
		                    i, property);
}

/******************************************************************************
 * xsurface property atom readers
 *****************************************************************************/

static void
read_surface_class(struct tw_xwm *xwm, struct tw_xsurface *surface,
                   xcb_get_property_reply_t *reply)
{
	size_t len;
	char *class;

	if (reply->type != XCB_ATOM_STRING &&
	    reply->type != xwm->atoms.utf8_string)
		return;
	len = xcb_get_property_value_length(reply);
	class = len ? xcb_get_property_value(reply) : NULL;
	tw_desktop_surface_set_class(&surface->dsurf, class, len);
}

static void
read_surface_title(struct tw_xwm *xwm, struct tw_xsurface *surface,
                   xcb_get_property_reply_t *reply)
{
	size_t len;
	char *title;

	if (reply->type != XCB_ATOM_STRING &&
	    reply->type != xwm->atoms.utf8_string)
		return;
	len = xcb_get_property_value_length(reply);
	title = len ? xcb_get_property_value(reply) : NULL;
	tw_desktop_surface_set_title(&surface->dsurf, title, len);
}

//TODO: this is probably not right. transient for is for surfaces like drop
//down mene, they should be the subsurfaces for tw_surface
static void
read_surface_parent(struct tw_xwm *xwm, struct tw_xsurface *surface,
                    xcb_get_property_reply_t *reply)
{
	struct tw_xsurface *parent_found = NULL;
	xcb_window_t *xid = xcb_get_property_value(reply);
	if (reply->type != XCB_ATOM_WINDOW)
		return;
	if (xid != NULL) {
		parent_found = tw_xsurface_from_id(xwm, *xid);
		//check if parent_found has us as the parent, which would
		//createa a loop
		if (!check_parent(parent_found, surface))
			surface->parent = parent_found;
		else
			tw_log_level(TW_LOG_WARN, "%d with %d would be in "
			             " a loop", surface->id, parent_found->id);

	} else
		surface->parent = NULL;
	tw_reset_wl_list(&surface->parent_link);
	if (surface->parent)
		wl_list_insert(&surface->parent->children,
		               &surface->parent_link);
}

static void
read_surface_pid(struct tw_xwm *xwm, struct tw_xsurface *surface,
                 xcb_get_property_reply_t *reply)
{

        if (reply->type != XCB_ATOM_CARDINAL)
		return;
        surface->pid = *(pid_t *)xcb_get_property_value(reply);
}

static inline void
read_surface_wintype(struct tw_xwm *xwm, struct tw_xsurface *surface,
                     xcb_get_property_reply_t *reply)
{
	xcb_atom_t *atoms = xcb_get_property_value(reply);

	if (reply->type != XCB_ATOM_ATOM)
		return;
	surface->win_type = *(xcb_atom_t *)atoms;
}

static void
read_surface_protocols(struct tw_xwm *xwm, struct tw_xsurface *surface,
                       xcb_get_property_reply_t *reply)
{
	xcb_atom_t *atoms = xcb_get_property_value(reply);

	if (reply->type != XCB_ATOM_ATOM)
		return;
	for (unsigned i = 0; i < reply->value_len; i++) {
		if (atoms[i] == xwm->atoms.wm_delete_window) {
			surface->support_delete = true;
			break;
		}
	}
}

static void
read_surface_net_wm_state(struct tw_xwm *xwm, struct tw_xsurface *surface,
                          xcb_get_property_reply_t *reply)
{
	bool fullscreend = false;
	bool max_vert = false;
	bool max_horz = false;
	bool hidden = false;
	struct tw_desktop_surface *dsurf = &surface->dsurf;
	xcb_atom_t *atom = xcb_get_property_value(reply);

	for (unsigned i = 0; i < reply->value_len; i++) {
		if (atom[i] == xwm->atoms.net_wm_state_fullscreen)
			fullscreend = true;
		else if (atom[i] == xwm->atoms.net_wm_state_maximized_horz)
			max_horz = true;
		else if (atom[i] == xwm->atoms.net_wm_state_maximized_vert)
			max_vert = true;
		else if (atom[i] == xwm->atoms.net_wm_state_hidden)
			hidden = true;
	}
	if (dsurf->surface_added) {
		if (fullscreend)
			tw_desktop_surface_set_fullscreen(dsurf, NULL, true);
		else if (max_vert && max_horz)
			tw_desktop_surface_set_maximized(dsurf, true);
		else if (hidden)
			tw_desktop_surface_set_minimized(dsurf);
	}
}

static void
read_surface_normal_hints(struct tw_xwm *xwm, struct tw_xsurface *surface,
                          xcb_get_property_reply_t *reply)
{
	bool has_min_size, has_max_size, has_base_size;
#if _TW_HAS_XCB_ICCCM
	xcb_size_hints_t hints;

	if (reply->value_len == 0)
		return;
	xcb_icccm_get_wm_size_hints_from_reply(&hints, reply);
	has_min_size = (hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) != 0;
	has_max_size = (hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) != 0;
	has_base_size = (hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) != 0;
#else
	struct tw_xnormal_hints hints;

	if (reply->value_len == 0)
		return;
	memcpy(&hints, xcb_get_property_value(reply),
	       sizeof(surface->normal_hints));
	has_min_size = (hints.flags & PMinSize) != 0;
	has_max_size = (hints.flags & PMaxSize) != 0;
	has_base_size = (hints.flags & PBaseSize) != 0;
#endif
	surface->normal_hints.min_aspect_den = hints.min_aspect_den;
	surface->normal_hints.min_aspect_num = hints.min_aspect_num;
	surface->normal_hints.min_height =
		(has_min_size) ? hints.min_height : 0;
	surface->normal_hints.min_width =
		(has_min_size) ? hints.min_width : 0;

        surface->normal_hints.max_aspect_den = hints.max_aspect_den;
	surface->normal_hints.max_aspect_num = hints.max_aspect_num;
	surface->normal_hints.max_height =
		(has_max_size) ? hints.max_height : INT_MAX;
	surface->normal_hints.max_width =
		(has_max_size) ? hints.max_width : INT_MAX;

	surface->normal_hints.base_height =
		(has_base_size) ? hints.base_height : hints.min_height;
	surface->normal_hints.base_width =
		(has_base_size) ? hints.base_width : hints.min_width;

}

static void
read_surface_motif_hints(struct tw_xwm *xwm, struct tw_xsurface *surface,
                         xcb_get_property_reply_t *reply)
{
	struct tw_xmotif_wm_hints *hints = xcb_get_property_value(reply);

	if (reply->value_len < 5)
		return;
	//read decorations
	if (hints->flags & MWM_HINTS_DECORATIONS) {
		uint32_t decorat = hints->decorations;
		//MWM_DECOR_ALL means all exept the value listed
		if ((decorat & MWM_DECOR_ALL)) {
			surface->decor_border = !(decorat & MWM_DECOR_BORDER);
			surface->decor_title = !(decorat & MWM_DECOR_TITLE);
		} else {
			surface->decor_border = decorat & MWM_DECOR_BORDER;
			surface->decor_title = decorat & MWM_DECOR_TITLE;
		}
	}
}

/*****************************************************************************
 * client message
 *****************************************************************************/

static void
read_wl_surface_id_msg(struct tw_xsurface *surface, struct tw_xwm *xwm,
                       xcb_client_message_event_t *ev)
{
	uint32_t id;
	struct wl_resource *wl_surface = NULL;

	id = ev->data.data32[0];
	wl_surface = wl_client_get_object(xwm->server->client, id);
	//We should have recevied MapRequested Already, so don't process the
	//window that was not mapped. At the same time, here is a chance we
	//received from xwayland this id but wl_object is not created yet from
	//wl_display, caching it for now.
	if (wl_surface && surface->pending_mapping) {
		tw_xsurface_map_tw_surface(
			surface, tw_surface_from_resource(wl_surface));
		surface->surface_id = 0;
	} else if (surface->pending_mapping) {
		surface->surface_id = id;
		surface->surface = NULL;
	}
}

static void
read_net_wm_state_msg(struct tw_xsurface *surface, struct tw_xwm *xwm,
                      xcb_client_message_event_t *ev)
{
	struct tw_desktop_surface *dsurf = &surface->dsurf;
	bool fullscreend = dsurf->fullscreened;
	bool maximized = dsurf->maximized;
	bool hidden = dsurf->minimized;
        uint32_t action = ev->data.data32[0];

        uint32_t property1 = ev->data.data32[1];
        uint32_t property2 = ev->data.data32[2];

        switch (action) {
        case _NET_WM_STATE_REMOVE:
	        fullscreend = false;
	        maximized = false;
	        hidden = false;
	        break;
        case _NET_WM_STATE_ADD:
	        fullscreend = true;
	        maximized = true;
	        hidden = true;
	        break;
        case _NET_WM_STATE_TOGGLE:
	        fullscreend = !fullscreend;
	        maximized = !maximized;
	        hidden = !hidden;
	        break;
        }

        //request the state to the implemenation.
        if ((property1 == xwm->atoms.net_wm_state_fullscreen) ||
            (property2 == xwm->atoms.net_wm_state_fullscreen))
	        tw_desktop_surface_set_fullscreen(dsurf, NULL, fullscreend);
        else if ((property1 == xwm->atoms.net_wm_state_maximized_vert) &&
                 (property2 == xwm->atoms.net_wm_state_maximized_horz))
	        tw_desktop_surface_set_maximized(dsurf, maximized);
        else if ((property1 == xwm->atoms.net_wm_state_maximized_horz) &&
                 (property2 == xwm->atoms.net_wm_state_maximized_vert))
	        tw_desktop_surface_set_maximized(dsurf, maximized);
        else if (((property1 == xwm->atoms.net_wm_state_hidden) ||
                  (property2 == xwm->atoms.net_wm_state_hidden)) && hidden)
	        tw_desktop_surface_set_minimized(dsurf);
}

static void
read_net_wm_moveresize_msg(struct tw_xsurface *surface, struct tw_xwm *xwm,
                           xcb_client_message_event_t *ev)
{
	uint32_t serial;
	bool move = false, cancel = false;
	enum wl_shell_surface_resize edge;
	int detail = ev->data.data32[2];

	switch (detail) {
	case _NET_WM_MOVERESIZE_SIZE_TOP:
		edge = WL_SHELL_SURFACE_RESIZE_TOP;
		break;
	case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
		edge = WL_SHELL_SURFACE_RESIZE_TOP_LEFT;
		break;
	case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
		edge = WL_SHELL_SURFACE_RESIZE_TOP_RIGHT;
		break;
	case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
		edge = WL_SHELL_SURFACE_RESIZE_BOTTOM;
		break;
	case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
		edge = WL_SHELL_SURFACE_RESIZE_BOTTOM;
		break;
	case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
		edge = WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT;
		break;
	case _NET_WM_MOVERESIZE_MOVE:
		move = true;
		break;
	case _NET_WM_MOVERESIZE_CANCEL:
		cancel = true;
		break;
	}

	if (cancel)
		return;
	//TODO error We would probably need assigning seat to
	serial = wl_display_next_serial(xwm->server->wl_display);
	if (move)
		tw_desktop_surface_move(&surface->dsurf, NULL, serial);
	else
		tw_desktop_surface_resize(&surface->dsurf, NULL, edge, serial);
}

static void
read_wm_protocols_msg(struct tw_xsurface *surface, struct tw_xwm *xwm,
                      xcb_client_message_event_t *ev)
{
	//TODO handle send pong back to wm.
}

/*****************************************************************************
 * handlers
 *****************************************************************************/

/* tw_desktop_surface need update window geometry on commit  */
static void
handle_commit_tw_xsurface(struct tw_surface *tw_surface)
{
	struct tw_xsurface *surface = tw_surface->role.commit_private;
	struct tw_desktop_surface *dsurf = &surface->dsurf;
	struct tw_desktop_manager *desktop = dsurf->desktop;
	pixman_region32_t surf_region;
	pixman_box32_t *r;

	pixman_region32_init(&surf_region);
	tw_desktop_surface_calc_window_geometry(tw_surface, &surf_region);
	r = pixman_region32_extents(&surf_region);
	dsurf->window_geometry.x = r->x1;
	dsurf->window_geometry.y = r->y1;
	dsurf->window_geometry.w = r->x2 - r->x1;
	dsurf->window_geometry.h = r->y2 - r->y1;
	pixman_region32_fini(&surf_region);

	if (dsurf->surface_added)
		desktop->api.committed(dsurf, desktop->user_data);
}

static void
handle_configure_tw_xsurface(struct tw_desktop_surface *dsurf,
                             enum wl_shell_surface_resize edge,
                             int32_t x, int32_t y,
                             unsigned width, unsigned height)
{
	struct tw_xsurface *surface =
		wl_container_of(dsurf, surface, dsurf);
	struct tw_xwm *xwm = surface->xwm;
	struct tw_surface *tw_surface = surface->surface;
	uint16_t mask = 0;
	uint32_t values[] = {x, y, width, height, 0};
	pixman_rectangle32_t geo = {
		dsurf->window_geometry.x, dsurf->window_geometry.y,
		dsurf->window_geometry.w, dsurf->window_geometry.h,
	};
	//updating net_wm_stae
	if (dsurf->focused)
		tw_xsurface_set_focus(surface, xwm);
	set_xsurface_net_wm_state(surface);

	//updating the size. In tw_xdg_view_set_position we have (x->gx,
	//y->gy), so here we need to revert it, as the client is expecting a
	//window geometry parameter.
	geo.x += tw_surface->geometry.x;
	geo.y += tw_surface->geometry.y;

	mask |= (geo.x != x) ? XCB_CONFIG_WINDOW_X : 0;
	mask |= (geo.y != y) ? XCB_CONFIG_WINDOW_Y : 0;
	mask |= (geo.width != width) ? XCB_CONFIG_WINDOW_WIDTH : 0;
	mask |= (geo.height != height) ? XCB_CONFIG_WINDOW_HEIGHT : 0;

	if (mask) {
		xcb_configure_window(xwm->xcb_conn, surface->id, mask, values);
		xcb_flush(xwm->xcb_conn);
	}
}

static void
handle_close_tw_xsurface(struct tw_desktop_surface *dsurf)
{
	struct tw_xsurface *surface =
		wl_container_of(dsurf, surface, dsurf);
	struct tw_xwm *xwm = surface->xwm;

	//if support delete
	if (surface->support_delete) {
		xcb_client_message_data_t msg = {0};
		msg.data32[0] = xwm->atoms.wm_delete_window;
		msg.data32[1] = XCB_CURRENT_TIME;
		send_surface_wm_msg(surface, &msg, XCB_EVENT_MASK_NO_EVENT);
	} else {
		xcb_kill_client(xwm->xcb_conn, surface->id);
	}
}

static void
handle_ping_tw_xsurface(struct tw_desktop_surface *dsurf, uint32_t serial)
{
	struct tw_xsurface *surface =
		wl_container_of(dsurf, surface, dsurf);
	struct tw_desktop_manager *manager = surface->xwm->manager;

	manager->api.pong(dsurf, manager->user_data);
}

static void
notify_xsurface_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xsurface *surface =
		wl_container_of(listener, surface, surface_destroy);
	tw_reset_wl_list(&surface->surface_destroy.link);
	tw_xsurface_unmap_requested(surface);
}

/******************************************************************************
 * exposed
 *****************************************************************************/

//reading property does not need updating net_wm_state
void
tw_xsurface_read_property(struct tw_xsurface *surface, xcb_atom_t type)
{
	struct tw_xwm *xwm = surface->xwm;
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn, 0, surface->id,
		                 type, XCB_ATOM_ANY, 0, 2048);
	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (!reply)
		return;

	if (type == XCB_ATOM_WM_CLASS)
		read_surface_class(xwm, surface, reply);
	else if (type == XCB_ATOM_WM_NAME || type == xwm->atoms.net_wm_name)
		read_surface_title(xwm, surface, reply);
	else if (type == XCB_ATOM_WM_TRANSIENT_FOR)
		read_surface_parent(xwm, surface, reply);
	else if (type == xwm->atoms.net_wm_pid)
		read_surface_pid(xwm, surface, reply);
	else if (type == xwm->atoms.net_wm_window_type)
		read_surface_wintype(xwm, surface, reply);
	else if (type == xwm->atoms.wm_protocols)
		read_surface_protocols(xwm, surface, reply);
	else if (type == xwm->atoms.net_wm_state)
		read_surface_net_wm_state(xwm, surface, reply);
	else if (type == xwm->atoms.wm_normal_hints)
		read_surface_normal_hints(xwm, surface, reply);
	else if (type == xwm->atoms.motif_wm_hints)
		read_surface_motif_hints(xwm, surface, reply);
	free(reply);
	xcb_flush(xwm->xcb_conn);
}

void
tw_xsurface_read_client_msg(struct tw_xsurface *surface,
                            xcb_client_message_event_t *ev)
{
	struct tw_xwm *xwm = surface->xwm;

	if (ev->type == xwm->atoms.wl_surface_id)
		read_wl_surface_id_msg(surface, xwm, ev);
	else if (ev->type == xwm->atoms.net_wm_state)
		read_net_wm_state_msg(surface, xwm, ev);
	else if (ev->type == xwm->atoms.net_wm_moveresize)
		read_net_wm_moveresize_msg(surface, xwm, ev);
	else if (ev->type == xwm->atoms.wm_protocols)
		read_wm_protocols_msg(surface, xwm, ev);
	xcb_flush(xwm->xcb_conn);
	//TODO getting selection message
}

void
tw_xsurface_map_tw_surface(struct tw_xsurface *surface,
                           struct tw_surface *tw_surface)
{
	struct tw_xwm *xwm = surface->xwm;
	struct tw_desktop_manager *manager = xwm->manager;
	struct tw_desktop_surface *dsurf = &surface->dsurf;
	const xcb_atom_t atoms[] = {
		XCB_ATOM_WM_CLASS,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_WM_TRANSIENT_FOR,
		xwm->atoms.wm_protocols,
		xwm->atoms.wm_normal_hints,
		xwm->atoms.motif_wm_hints,
		xwm->atoms.net_wm_state,
		xwm->atoms.net_wm_window_type,
		xwm->atoms.net_wm_name,
		xwm->atoms.net_wm_pid,
	};

	//setting roles
	if (tw_surface_has_role(tw_surface) &&
	    tw_surface->role.commit != handle_commit_tw_xsurface) {
		wl_resource_post_error(tw_surface->resource, 0,
		                       "wl_surface@d already has role");
		return;
	}
	tw_surface->role.commit = handle_commit_tw_xsurface;
	tw_surface->role.commit_private = surface;
	tw_surface->role.name = "xwayland surface";

	//remove last desktop surface if any.
	if (dsurf->surface_added)
		tw_xsurface_unmap_requested(surface);
	//reinitialize surface
	tw_desktop_surface_init(dsurf, tw_surface->resource, NULL, manager);
	dsurf->configure = handle_configure_tw_xsurface;
	dsurf->close = handle_close_tw_xsurface;
	dsurf->ping = handle_ping_tw_xsurface;
	tw_set_resource_destroy_listener(tw_surface->resource,
	                                 &surface->surface_destroy,
	                                 notify_xsurface_surface_destroy);

	for (unsigned i = 0; i < sizeof(atoms)/sizeof(xcb_atom_t); i++)
		tw_xsurface_read_property(surface, atoms[i]);

	surface->surface = tw_surface;
	surface->surface_id = 0;
	surface->pending_mapping = false;
	tw_desktop_surface_add(dsurf);
}

void
tw_xsurface_unmap_requested(struct tw_xsurface *surface)
{
	struct tw_desktop_surface *dsurf = &surface->dsurf;

	set_xsurface_wm_state(surface, ICCCM_WITHDRAWN_STATE);

	surface->pending_mapping = false;
	tw_desktop_surface_rm(dsurf);
	tw_desktop_surface_fini(dsurf);
}

void
tw_xsurface_map_requested(struct tw_xsurface *xsurface)
{
	struct tw_xwm *xwm = xsurface->xwm;
	uint32_t values = XCB_STACK_MODE_BELOW;

	set_xsurface_wm_state(xsurface, ICCCM_NORMAL_STATE);
	set_xsurface_net_wm_state(xsurface);

        //from weston documentation. The MapRequest happens before the
	//wl_surface.id is available. Here we Simply acknowledge the xwindow
	//for the mapping. Processing happens later in reading_wl_surface_id.
	xcb_configure_window(xwm->xcb_conn, xsurface->id,
	                     XCB_CONFIG_WINDOW_STACK_MODE, &values);
	xcb_map_window(xwm->xcb_conn, xsurface->id);
	xsurface->pending_mapping = true;
}

//TODO replaces it with hash map?
struct tw_xsurface *
tw_xsurface_from_id(struct tw_xwm *xwm, xcb_window_t id)
{
	struct tw_xsurface *surface;

	wl_list_for_each(surface, &xwm->surfaces, link)
		if (surface->id == id)
			return surface;
	return NULL;
}

void
tw_xsurface_set_focus(struct tw_xsurface *surface, struct tw_xwm *xwm)
{
	xcb_client_message_event_t msg;

	if (surface) {
		uint32_t values[1];

		if (surface->override_redirect)
			return;
		msg.response_type = XCB_CLIENT_MESSAGE;
		msg.format = 32;
		msg.window = surface->id;
		msg.type = xwm->atoms.wm_protocols;
		msg.data.data32[0] = xwm->atoms.wm_take_focus;
		msg.data.data32[1] = XCB_TIME_CURRENT_TIME;

		xcb_send_event(xwm->xcb_conn, 0, surface->id,
		               XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
		               (char *)&msg);
		xcb_set_input_focus(xwm->xcb_conn,
		                    XCB_INPUT_FOCUS_POINTER_ROOT,
		                    surface->id, XCB_TIME_CURRENT_TIME);
		//move surface to toplevel
		values[0] = XCB_STACK_MODE_ABOVE;
		xcb_configure_window(xwm->xcb_conn, surface->id,
		                     XCB_CONFIG_WINDOW_STACK_MODE, values);
		set_xsurface_net_wm_state(surface);

	} else {
		xcb_set_input_focus(xwm->xcb_conn,
		                    XCB_INPUT_FOCUS_POINTER_ROOT,
		                    XCB_NONE, XCB_TIME_CURRENT_TIME);
	}
	xwm->focus_window = surface;
}

struct tw_xsurface *
tw_xsurface_create(struct tw_xwm *xwm, xcb_window_t win_id,
                   int x, int y, unsigned w, unsigned h,
                   bool override_redirect)
{
	uint32_t values = XCB_EVENT_MASK_FOCUS_CHANGE |
		XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_get_geometry_cookie_t geometry_cookie;
        xcb_get_geometry_reply_t *geometry_reply = NULL;

	struct tw_xsurface *surface = calloc(1, sizeof(*surface));

	if (!surface)
		return NULL;
	surface->id = win_id;
	//have to set desktop here so surface never mapped wont crash
	surface->dsurf.desktop = xwm->manager;
	surface->dsurf.window_geometry.x = x;
	surface->dsurf.window_geometry.y = y;
	surface->dsurf.window_geometry.w = w;
	surface->dsurf.window_geometry.h = h;
	surface->override_redirect = override_redirect;
	surface->xwm = xwm;
	wl_list_init(&surface->link);
	wl_list_init(&surface->children);
	wl_list_init(&surface->parent_link);
	wl_list_init(&surface->surface_destroy.link);

	geometry_cookie = xcb_get_geometry(xwm->xcb_conn, win_id);
	xcb_change_window_attributes(xwm->xcb_conn, win_id, XCB_CW_EVENT_MASK,
	                             &values);

	geometry_reply = xcb_get_geometry_reply(xwm->xcb_conn, geometry_cookie,
	                                        NULL);
	if (geometry_reply != NULL)
		surface->has_alpha = geometry_reply->depth == 32;
	free(geometry_reply);

	return surface;
}

//this should ne called on xcb_window destroyed
void
tw_xsurface_destroy(struct tw_xsurface *surface)
{
	tw_xsurface_unmap_requested(surface);
	tw_reset_wl_list(&surface->surface_destroy.link);
	//there are other things.
	free(surface);
}
