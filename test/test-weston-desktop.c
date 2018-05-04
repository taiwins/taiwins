/*
 * not-a-wm - The tiniest possible Wayland compositor based on libweston and libweston-desktop
 *
 * Copyright © 2017 Quentin "Sardem FF7" Glidic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include <linux/input.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <compositor.h>
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <windowed-output-api.h>
#include <libweston-desktop.h>

typedef struct {
    struct wl_display *display;
    struct weston_compositor *compositor;
    struct wl_listener output_pending_listener;
    union {
	struct weston_drm_backend_config drm;
	struct weston_wayland_backend_config wayland;
	struct weston_x11_backend_config x11;
    } backend_config;
    union {
	const struct weston_windowed_output_api *windowed;
	const struct weston_drm_output_api *drm;
    } api;
    struct weston_layer background_layer;
    struct weston_surface *background;
    struct weston_view *background_view;
    struct weston_desktop *desktop;
    struct weston_layer surfaces_layer;
} NawContext;

typedef struct {
    struct weston_desktop_surface *desktop_surface;
    struct weston_surface *surface;
    struct weston_view *view;
} NawSurface;

static int
_naw_log(const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

static void
_naw_output_pending_drm(struct wl_listener *listener, void *data)
{
    NawContext *context = wl_container_of(listener, context, output_pending_listener);
    struct weston_output *woutput = data;

    /* We enable all DRM outputs with their preferred mode */
    context->api.drm->set_mode(woutput, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
    context->api.drm->set_gbm_format(woutput, NULL);
    context->api.drm->set_seat(woutput, NULL);
    weston_output_set_scale(woutput, 1);
    weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
    weston_output_enable(woutput);
}

static void
_naw_output_pending_virtual(struct wl_listener *listener, void *data)
{
    NawContext *context = wl_container_of(listener, context, output_pending_listener);
    struct weston_output *woutput = data;

    /* We enable all windowed outputs with a 800×600 mode */
    weston_output_set_scale(woutput, 1);
    weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
    context->api.windowed->output_set_size(woutput, 800, 600);
    weston_output_enable(woutput);
}

static void
_naw_exit(struct weston_compositor *compositor)
{
    NawContext *context = weston_compositor_get_user_data(compositor);
    wl_display_terminate(context->display);
}

static void
_naw_surface_added(struct weston_desktop_surface *surface, void *user_data)
{
    NawContext *context = user_data;

    /* Creating our own wrapper struct to store whatever we need, specifically the view */
    NawSurface *self;
    self = calloc(1, sizeof(NawSurface));
    self->desktop_surface = surface;

    weston_desktop_surface_set_user_data(self->desktop_surface, self);

    self->surface = weston_desktop_surface_get_surface(self->desktop_surface);
    self->view = weston_desktop_surface_create_view(self->desktop_surface);

    weston_layer_entry_insert(&context->surfaces_layer.view_list, &self->view->layer_link);

    /* No real WM work here, we just put all surfaces at (0,0) */
    weston_view_set_position(self->view, 0, 0);

    weston_surface_damage(self->surface);
    weston_compositor_schedule_repaint(context->compositor);

    /* TODO: There, you usually change focus if according to your defined policy */
}

static void
_naw_surface_removed(struct weston_desktop_surface *surface, void *user_data)
{
    NawContext *context = user_data;
    NawSurface *self = weston_desktop_surface_get_user_data(surface);

    if ( self == NULL )
	return;

    weston_desktop_surface_unlink_view(self->view);
    weston_view_destroy(self->view);
    weston_desktop_surface_set_user_data(surface, NULL);
    free(self);

    /* TODO: There, you usually change focus if the removed surface was focused */
    (void) context;
}

static const struct weston_desktop_api _naw_desktop_api = {
    /* For backward ABI compatibility */
    .struct_size = sizeof(struct weston_desktop_api),

    /* These two are the minimal API allowed */
    .surface_added = _naw_surface_added,
    .surface_removed = _naw_surface_removed,
};


void log_on_key(struct weston_keyboard *keyboard, const struct timespec *time, uint32_t key, void *data)
{
	weston_log("this key pressed");
}

int
main()
{
    NawContext context_ = { .display = NULL }, *context = &context_;

    weston_log_set_handler(_naw_log, _naw_log);

    /* Ignore SIGPIPE as it is useless */
    signal(SIGPIPE, SIG_IGN);

    context->display = wl_display_create();
    context->compositor = weston_compositor_create(context->display, context);

    /* Just using NULL here will use environment variables */
    weston_compositor_set_xkb_rule_names(context->compositor, NULL);

    enum weston_compositor_backend backend = WESTON_BACKEND_DRM;
    if ( getenv("WAYLAND_DISPLAY") != NULL )
	backend = WESTON_BACKEND_WAYLAND;
    else if ( getenv("DISPLAY") != NULL )
	backend = WESTON_BACKEND_X11;

    switch ( backend )
    {
    case WESTON_BACKEND_DRM:
	context->backend_config.drm.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
	context->backend_config.drm.base.struct_size = sizeof(struct weston_drm_backend_config);
    break;
    case WESTON_BACKEND_WAYLAND:
	context->backend_config.wayland.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
	context->backend_config.wayland.base.struct_size = sizeof(struct weston_wayland_backend_config);
    break;
    case WESTON_BACKEND_X11:
	context->backend_config.x11.base.struct_version = WESTON_X11_BACKEND_CONFIG_VERSION;
	context->backend_config.x11.base.struct_size = sizeof(struct weston_x11_backend_config);
    break;
    default:
	/* Not supported */
	return 1;
    }
    weston_compositor_load_backend(context->compositor, backend, &context->backend_config.drm.base);

    switch ( backend )
    {
    case WESTON_BACKEND_DRM:
	context->api.drm = weston_drm_output_get_api(context->compositor);
	if ( context->api.drm == NULL )
	    return 1;

	context->output_pending_listener.notify = _naw_output_pending_drm;
    break;
    case WESTON_BACKEND_X11:
    case WESTON_BACKEND_WAYLAND:
    {
	context->api.windowed = weston_windowed_output_get_api(context->compositor);
	if ( context->api.windowed == NULL )
	    return 1;

	context->api.windowed->output_create(context->compositor, "W1");

	context->output_pending_listener.notify = _naw_output_pending_virtual;
    }
    break;
    default:
	return 1;
    }

    wl_signal_add(&context->compositor->output_pending_signal, &context->output_pending_listener);
    weston_pending_output_coldplug(context->compositor);

    context->compositor->vt_switching = 1;
    context->compositor->exit = _naw_exit;

    /* We want a defined background */
    weston_layer_init(&context->background_layer, context->compositor);
    weston_layer_set_position(&context->background_layer, WESTON_LAYER_POSITION_BACKGROUND);
    context->background = weston_surface_create(context->compositor);
    weston_surface_set_size(context->background, 8096, 8096);
    weston_surface_set_color(context->background, 0, 0.25, 0.5, 1);
    context->background_view = weston_view_create(context->background);
    weston_layer_entry_insert(&context->background_layer.view_list, &context->background_view->layer_link);

    context->desktop = weston_desktop_create(context->compositor, &_naw_desktop_api, context);
    weston_layer_init(&context->surfaces_layer, context->compositor);
    weston_layer_set_position(&context->surfaces_layer, WESTON_LAYER_POSITION_NORMAL);


    const char *socket_name;
    socket_name = wl_display_add_socket_auto(context->display);
    if ( socket_name == NULL )
    {
	weston_log("Couldn’t add socket: %s\n", strerror(errno));
	return -1;
    }

    setenv("WAYLAND_DISPLAY", socket_name, 1);
    unsetenv("DISPLAY");
    struct weston_seat seat;
    weston_seat_init(&seat, context->compositor, "default");
    struct weston_pointer *p = weston_pointer_create(&seat);
    weston_seat_init_pointer(&seat);
//    struct weston_keyboard *k =  weston_keyboard_create();
    //keybindings is a list. Every time you press key, we need to run through the list
    weston_compositor_add_key_binding(context->compositor, KEY_0, 0, log_on_key, NULL);

    weston_compositor_wake(context->compositor);
    wl_display_run(context->display);

    weston_desktop_destroy(context->desktop);
    weston_compositor_destroy(context->compositor);

    return 0;
}
