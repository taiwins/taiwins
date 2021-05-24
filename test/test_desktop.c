#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat_grab.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>

#include <taiwins/engine.h>
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
		dsurf = tw_xwayland_desktop_surface_from_tw_surface(surface);
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
                    const struct view_configure *conf, uint32_t flags)
{
	struct tw_engine_seat *seat =
		tw_engine_get_focused_seat(desktop->engine);
	struct tw_seat *tw_seat = seat->tw_seat;
	struct tw_surface *surf = dsurf->tw_surface;
	bool focused = view_focused(dsurf->tw_surface, desktop);
	flags |= TW_DESKTOP_SURFACE_CONFIG_X |
		TW_DESKTOP_SURFACE_CONFIG_Y |
		TW_DESKTOP_SURFACE_CONFIG_W |
		TW_DESKTOP_SURFACE_CONFIG_H;
	flags |= focused ? TW_DESKTOP_SURFACE_FOCUSED : 0;

	//first one on the list
	tw_surface_set_position(dsurf->tw_surface, conf->rect.x, conf->rect.y);
	tw_desktop_surface_send_configure(dsurf, 0,
	                                  conf->rect.x, conf->rect.y,
	                                  conf->rect.width, conf->rect.height,
	                                  flags);
	if (focused) {
		if ((tw_seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) &&
		    focused)
			tw_keyboard_notify_enter(&tw_seat->keyboard,
			                         surf->resource, NULL, 0);
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
	send_view_configure(dsurf, desktop, &conf, 0);
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
		send_view_configure(dsurf, desktop, &conf, 0);
	}
}

//TODO: missing moving and resizing grab

/******************************************************************************
 * grab
 *****************************************************************************/
static struct test_desktop_grab {
	struct tw_seat_pointer_grab pointer_grab;
	struct wl_listener surface_remove;
	struct tw_desktop_surface *curr;
	double gx, gy;
	enum wl_shell_surface_resize edge;
} TEST_GRAB = {0};

static void
test_grab_start(struct test_desktop_grab *tg, struct tw_desktop_surface *dsurf,
                struct tw_test_desktop *desktop, struct tw_pointer *pointer,
                const struct tw_pointer_grab_interface *gi,
                enum wl_shell_surface_resize edge)
{
	if (pointer->grab != &pointer->default_grab)
		return;
	tg->edge = edge;
	tg->curr = dsurf;
	tg->gx = nan("");
	tg->gy = nan("");
	tg->pointer_grab.impl = gi;
	tg->pointer_grab.data = desktop;
	tw_pointer_start_grab(pointer, &tg->pointer_grab);
}

static void
handle_pointer_grab_cancel(struct tw_seat_pointer_grab *grab)
{
	struct test_desktop_grab *tg =
		wl_container_of(grab, tg, pointer_grab);
	tg->curr = NULL;
	tg->gx = nan("");
	tg->gy = nan("");
}

static void
handle_move_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct test_desktop_grab *tg =
		wl_container_of(grab, tg, pointer_grab);
	struct tw_desktop_surface *dsurf = tg->curr;
	struct tw_surface *surf = NULL;
	float gx, gy;

	if (!dsurf)
		return;
	surf = dsurf->tw_surface;
	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);
	if (!isnan(tg->gx) && !isnan(tg->gy))
		tw_surface_set_position(surf, surf->geometry.x + (gx-tg->gx),
		                        surf->geometry.y + gy-tg->gy);

	tg->gx = gx;
	tg->gy = gy;
}

static void
handle_pointer_grab_button(struct tw_seat_pointer_grab *grab,
	                   uint32_t time_msec, uint32_t button,
	                   enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
	    pointer->btn_count == 0)
		tw_pointer_end_grab(pointer);
}

static const struct tw_pointer_grab_interface move_pointer_grab_impl = {
	.motion = handle_move_pointer_grab_motion,
	.button = handle_pointer_grab_button,
	.cancel = handle_pointer_grab_cancel,
};

static void
handle_resize_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct test_desktop_grab *tg =
		wl_container_of(grab, tg, pointer_grab);
	struct tw_surface *surf;
	struct tw_test_desktop *desktop = grab->data;
	float gx, gy;
	//would change position, bail
	if ((tg->edge & WL_SHELL_SURFACE_RESIZE_TOP_LEFT))
		return;
	if (!tg->curr)
		return;
	surf = tg->curr->tw_surface;
	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);

	if (!isnan(tg->gx) && !isnan(tg->gy)) {
		float dw = (gx-tg->gx);
		float dh = (gy-tg->gy);
		struct view_configure conf = {
			.rect.x = surf->geometry.x, //keep the same pos
			.rect.y = surf->geometry.y, //keep the same pos
			.rect.width = tg->curr->window_geometry.w + dw,
			.rect.height = tg->curr->window_geometry.h + dh,
		};
		send_view_configure(tg->curr, desktop, &conf, 0);
	}
	tg->gx = gx;
	tg->gy = gy;

}

static const struct tw_pointer_grab_interface resize_pointer_grab_impl = {
	.motion = handle_resize_pointer_grab_motion,
	.button = handle_pointer_grab_button, // same as move grab
	.cancel = handle_pointer_grab_cancel, // same as move grab
};


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
	send_view_configure(dsurf, desktop, &conf, 0);
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
		tw_keyboard_notify_enter(&seat->keyboard, surf->resource,
		                         NULL, 0);

	/* int32_t gx = dsurf->window_geometry.x; */
	/* int32_t gy = dsurf->window_geometry.y; */

	/* tw_surface_set_position(surf, float x, float y) */
}

static void
handle_surface_show_window_menu(struct tw_desktop_surface *surface,
                                struct tw_seat *seat,
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
                    struct tw_seat *seat, uint32_t serial,
                    void *user_data)
{
	struct tw_desktop_manager *manager = dsurf->desktop;
	struct tw_test_desktop *desktop =
		wl_container_of(manager, desktop, manager);

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl_level(TW_LOG_WARN,
		              "serial not matched for desktop moving grab.");
		return;
	}
	test_grab_start(&TEST_GRAB, dsurf, desktop, &seat->pointer,
	                &move_pointer_grab_impl, 0);
}

static void
handle_surface_resize(struct tw_desktop_surface *dsurf,
                      struct tw_seat *seat, uint32_t serial,
                      enum wl_shell_surface_resize edge, void *user_data)
{
	struct tw_desktop_manager *manager = dsurf->desktop;
	struct tw_test_desktop *desktop =
		wl_container_of(manager, desktop, manager);

	if (!tw_seat_valid_serial(seat, serial)) {
		tw_logl_level(TW_LOG_WARN,
		              "serial not matched for desktop moving grab.");
		return;
	}
	test_grab_start(&TEST_GRAB, dsurf, desktop, &seat->pointer,
	                &resize_pointer_grab_impl, edge);
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
	uint32_t flags = fullscreen ? TW_DESKTOP_SURFACE_FULLSCREENED : 0;

	tw_reset_wl_list(view_link(surf));
	wl_list_insert(&desktop->layer.views, view_link(surf));
	send_view_configure(dsurf, desktop, &conf, flags);
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
	uint32_t flags = maximized ? TW_DESKTOP_SURFACE_MAXIMIZED : 0;

	tw_reset_wl_list(view_link(surf));
	wl_list_insert(&desktop->layer.views, view_link(surf));
	send_view_configure(dsurf, desktop, &conf, flags);
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

static void
handle_config_request(struct tw_desktop_surface *dsurf,
                      int x, int y, unsigned w, unsigned h, uint32_t flags,
                      void *user_data)
{
	tw_desktop_surface_send_configure(dsurf, 0, x, y, w, h, flags);
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
	.configure_requested = handle_config_request,
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
