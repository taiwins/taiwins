#include "taiwins/engine.h"
#include "taiwins/objects/layers.h"
#include "taiwins/objects/seat.h"
#include <stdbool.h>
#include <stdlib.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/output_device.h>
#ifdef _TW_HAS_XWAYLAND
#include <taiwins/xwayland.h>
#endif

#include "test_desktop.h"

#define TW_VIEW_LINK 4

struct view_configure {
	pixman_rectangle32_t rect;
};

static inline struct wl_list *
view_link(struct tw_surface *surface)
{
	return &surface->layer_link;
}

static inline struct tw_surface *
view_from_link(struct wl_list *link)
{
	struct tw_surface *surf = wl_container_of(link, surf, layer_link);
	return surf;
}

static inline struct tw_surface *
get_curr_focused(struct tw_test_desktop *desktop)
{
	return wl_list_empty(&desktop->layer.views) ? NULL :
		view_from_link(desktop->layer.views.next);
}

static inline struct tw_desktop_surface *
dsurf_from_view(struct tw_surface *surface)
{
	struct tw_desktop_surface *dsurf =
		tw_desktop_surface_from_tw_surface(surface);
#ifdef _TW_HAS_XWAYLAND
	if (!dsurf)
		dsurf = tw_desktop_surface_from_tw_surface(surface);
#endif
	return dsurf;
}

#define view_for_each(surf, list) \
	for (surf = 0, surf = view_from_link((list)->next); \
	     view_link(surf) != (list); \
	     surf = view_from_link(view_link(surf)->next))

static inline bool
view_find(struct tw_desktop_surface *dsurf, struct tw_test_desktop *desktop)
{
	struct tw_surface *surf = NULL;

	view_for_each(surf, &desktop->layer.views) {
		if (surf == dsurf->tw_surface)
			return true;
	}
	return false;
}

static inline pixman_rectangle32_t
view_get_visible(struct tw_desktop_surface *dsurf)
{
	struct tw_surface *surf = dsurf->tw_surface;
	pixman_rectangle32_t rect = {
		surf->geometry.x + dsurf->window_geometry.x,
		surf->geometry.y + dsurf->window_geometry.y,
		dsurf->window_geometry.w,
		dsurf->window_geometry.h,
	};
	return rect;
}

static inline bool
view_focused(struct tw_surface *surf, const struct tw_test_desktop *desktop)
{
	return view_link(surf) == desktop->layer.views.next;
}

static void
send_view_configure(struct tw_desktop_surface *dsurf,
                    const struct tw_test_desktop *desktop,
                    const struct view_configure *conf)
{
	struct tw_engine_seat *seat =
		tw_engine_get_focused_seat(desktop->engine);
	struct tw_seat *tw_seat = seat->tw_seat;
	struct tw_surface *surf = dsurf->tw_surface;

	dsurf->tiled_state = 0;
	//first one on the list
	dsurf->focused = view_focused(dsurf->tw_surface, desktop);

	dsurf->configure(dsurf, 0, //edge
	                 conf->rect.x, conf->rect.y,
	                 conf->rect.width, conf->rect.height);
	if (dsurf->focused) {
		if ((tw_seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) &&
		    dsurf->focused)
			tw_keyboard_set_focus(&tw_seat->keyboard,
			                      surf->resource, NULL);
		dsurf->ping(dsurf, wl_display_next_serial(desktop->display));
	}
}

static inline void
set_view_focus(struct tw_surface *surf, const struct tw_test_desktop *desktop)
{
	struct tw_desktop_surface *dsurf = dsurf_from_view(surf);
	struct view_configure conf = {
		.rect = view_get_visible(dsurf),
	};
	send_view_configure(dsurf, desktop, &conf);
}

static void
handle_refocus(void *data)
{
	struct tw_test_desktop *desktop = data;
	struct tw_surface *surf = NULL;

	view_for_each(surf, &desktop->layer.views) {
		struct tw_desktop_surface *dsurf =
			dsurf_from_view(surf);
		struct view_configure conf = {
			.rect = view_get_visible(dsurf),
		};
		send_view_configure(dsurf, desktop, &conf);
	}
}

//TODO: missing moving and resizing grab

/******************************************************************************
 * handles
 *****************************************************************************/

static void
handle_ping_timeout(struct tw_desktop_surface *client,
                    void *user_data)
{
}

static void
handle_pong(struct tw_desktop_surface *client,
            void *user_data)
{}

static void
handle_surface_added(struct tw_desktop_surface *dsurf, void *user_data)
{
	struct tw_test_desktop *desktop = user_data;
	struct tw_engine_output *output =
		tw_engine_get_focused_output(desktop->engine);
	pixman_rectangle32_t region =
		tw_output_device_geometry(output->device);
	struct view_configure conf = {
		.rect.x = rand() % (region.width / 2),
		.rect.y = rand() % (region.height / 2),
		.rect.width = 800,
		.rect.height = 400,
	};
	struct tw_surface *prev_surf = get_curr_focused(desktop);

	wl_list_insert(&desktop->layer.views, view_link(dsurf->tw_surface));
	//randomly setup a size
	send_view_configure(dsurf, desktop, &conf);
	//unset the previous view focus
	if (prev_surf)
		set_view_focus(prev_surf, desktop);
}

static void
handle_surface_removed(struct tw_desktop_surface *dsurf,
		       void *user_data)
{
	struct tw_test_desktop *desktop = user_data;
	struct wl_event_loop *loop =
		wl_display_get_event_loop(desktop->display);

        if (!view_find(dsurf, desktop))
	        return;
        tw_reset_wl_list(view_link(dsurf->tw_surface));
	//This is a HACK, because we are in a wl_resource_destroy_listener,
	//refocus the keyboard will reference the deleted wl_surface. Here we
	//have to wait a idle event for keyboard the clear the focus itself.
	wl_event_loop_add_idle(loop, handle_refocus, desktop);
}

static void
handle_surface_committed(struct tw_desktop_surface *dsurf,
                         void *user_data)
{
	//THIS is a hack, somehow the surface doesn't have keyboard on
	//wl_surface_add
	struct tw_surface *surf = dsurf->tw_surface;
	struct tw_desktop_manager *manager = dsurf->desktop;
	struct tw_test_desktop *desktop =
		wl_container_of(manager, desktop, manager);
	struct tw_seat *seat =
		tw_engine_get_focused_seat(desktop->engine)->tw_seat;
	if (view_focused(dsurf->tw_surface, desktop) &&
	    seat->keyboard.focused_surface != surf->resource)
		tw_keyboard_set_focus(&seat->keyboard, surf->resource, NULL);

	/* int32_t gx = dsurf->window_geometry.x; */
	/* int32_t gy = dsurf->window_geometry.y; */

	/* tw_surface_set_position(surf, float x, float y) */
}

static void
handle_surface_show_window_menu(struct tw_desktop_surface *surface,
                                struct wl_resource *seat,
                                int32_t x, int32_t y,
                                void *user_data)
{
	//TODO implementation
	tw_logl("desktop_surface@%d requires show window menu",
	        wl_resource_get_id(surface->resource));
}

static void
handle_set_parent(struct tw_desktop_surface *surface,
                  struct tw_desktop_surface *parent,
                  void *user_data)
{
	//TODO implementation
	tw_logl("desktop_surface@%d requires set_parent",
	        wl_resource_get_id(surface->resource));
}

static void
handle_surface_move(struct tw_desktop_surface *dsurf,
                    struct wl_resource *seat_resource, uint32_t serial,
                    void *user_data)
{
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("serial not matched for desktop moving grab.");
		return;
	}
	tw_logl("Should start moving grab for surface@%d!",
		wl_resource_get_id(dsurf->tw_surface->resource));
}

static void
handle_surface_resize(struct tw_desktop_surface *dsurf,
                      struct wl_resource *seat_resource, uint32_t serial,
                      enum wl_shell_surface_resize edge, void *user_data)
{
	struct tw_seat *seat = tw_seat_from_resource(seat_resource);

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl("serial not matched for desktop moving grab.");
		return;
	}
	tw_logl("Should start resizing grab for surface@%d!",
		wl_resource_get_id(dsurf->tw_surface->resource));
}

static void
handle_fullscreen(struct tw_desktop_surface *dsurf,
                  struct wl_resource *output, bool fullscreen,
                  void *user_data)
{
	struct tw_test_desktop *desktop = user_data;
	struct tw_engine_output *engine_output = output ?
		tw_engine_output_from_resource(desktop->engine, output) :
		tw_engine_get_focused_output(desktop->engine);
	struct tw_surface *surf = dsurf->tw_surface;
	struct tw_surface *prev_surf = get_curr_focused(desktop);
	pixman_rectangle32_t region =
		tw_output_device_geometry(engine_output->device);
	pixman_rectangle32_t visible =
		view_get_visible(dsurf);
	struct view_configure conf = {
		.rect = (fullscreen) ? region : visible,
	};

	tw_reset_wl_list(view_link(surf));
	wl_list_insert(&desktop->layer.views, view_link(surf));
	dsurf->fullscreened = fullscreen;
	send_view_configure(dsurf, desktop, &conf);
	//fullscreen this one may lead defocus of others.
	if (prev_surf != surf && prev_surf)
		set_view_focus(prev_surf, desktop);
}

static void
handle_maximized(struct tw_desktop_surface *dsurf, bool maximized,
                 void *user_data)
{
	struct tw_test_desktop *desktop = user_data;
	struct tw_engine_output *engine_output =
		tw_engine_get_focused_output(desktop->engine);
	struct tw_surface *surf = dsurf->tw_surface;
	struct tw_surface *prev_surf = get_curr_focused(desktop);
	pixman_rectangle32_t region =
		tw_output_device_geometry(engine_output->device);
	pixman_rectangle32_t visible =
		view_get_visible(dsurf);
	struct view_configure conf = {
		.rect = (maximized) ? region : visible,
	};

	tw_reset_wl_list(view_link(surf));
	wl_list_insert(&desktop->layer.views, view_link(surf));
	dsurf->maximized = maximized;
	send_view_configure(dsurf, desktop, &conf);
	//maximized this one may lead defocus of others.
	if (prev_surf != surf && prev_surf)
		set_view_focus(prev_surf, desktop);
}

static void
handle_minimized(struct tw_desktop_surface *dsurf, void *user_data)
{
	struct tw_test_desktop *desktop = user_data;
	struct tw_surface *surface = dsurf->tw_surface;

	tw_reset_wl_list(view_link(surface));
	set_view_focus(surface, desktop);
}

static const struct tw_desktop_surface_api test_api = {
	.ping_timeout = handle_ping_timeout,
	.pong = handle_pong,
	.surface_added = handle_surface_added,
	.surface_removed = handle_surface_removed,
	.committed = handle_surface_committed,
	.set_parent = handle_set_parent,
	.show_window_menu = handle_surface_show_window_menu,
	.move = handle_surface_move,
	.resize = handle_surface_resize,
	.fullscreen_requested = handle_fullscreen,
	.maximized_requested = handle_maximized,
	.minimized_requested = handle_minimized,
};

void
tw_test_desktop_init(struct tw_test_desktop *desktop,
                     struct tw_engine *engine)
{
	desktop->engine = engine;
	desktop->display = engine->display;
	tw_desktop_init(&desktop->manager, engine->display, &test_api,
	                desktop, TW_DESKTOP_INIT_INCLUDE_XDG_SHELL_STABEL |
	                TW_DESKTOP_INIT_INCLUDE_WL_SHELL);
	tw_layer_init(&desktop->layer);
	tw_layer_set_position(&desktop->layer, TW_LAYER_POS_DESKTOP_FRONT,
	                      &desktop->engine->layers_manager);
}

void
tw_test_desktop_fini(struct tw_test_desktop *desktop)
{
	tw_layer_unset_position(&desktop->layer);
}
