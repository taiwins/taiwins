#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <wayland-server.h>

#include <compositor.h>
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <windowed-output-api.h>
#include <libweston-desktop.h>



struct tw_backend {
	struct weston_compositor *compositor;
	union {
		struct weston_drm_backend_config drm;
		struct weston_wayland_backend_config wayland;
		struct weston_x11_backend_config x11;
	} backend_config;
	union {
		const struct weston_drm_output_api *drm;
		const struct weston_windowed_output_api *window;
	} api;
	struct wl_listener output_pending_listener;
	struct weston_layer layer_background;
	struct weston_surface *surface_background;
	struct weston_view *view_background;
};


//you know, we
static struct tw_backend TWbackend;

struct tw_backend *
tw_get_backend(void)
{
	return &TWbackend;
}


static void
output_pending_drm(struct wl_listener *listener, void *data)
{
	struct tw_backend *context = wl_container_of(listener, context, output_pending_listener);
	struct weston_output *woutput = (struct weston_output *)data;

	/* We enable all DRM outputs with their preferred mode */
	context->api.drm->set_mode(woutput, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
	context->api.drm->set_gbm_format(woutput, NULL);
	context->api.drm->set_seat(woutput, NULL);
	weston_output_set_scale(woutput, 1);
	weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_enable(woutput);
}

static void
output_pending_windowed(struct wl_listener *listener, void *data)
{
	struct tw_backend *context = wl_container_of(listener, context, output_pending_listener);
	struct weston_output *woutput = (struct weston_output *)data;

	//We enable all windowed outputs with a 800×600 mode
	weston_output_set_scale(woutput, 1);
	weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
	context->api.window->output_set_size(woutput, 1000, 1000);
	weston_output_enable(woutput);
}



bool
tw_setup_backend(struct weston_compositor *compositor)
{
	enum weston_compositor_backend backend;
	struct tw_backend *b = &TWbackend;
	b->compositor = compositor;

	if ( getenv("WAYLAND_DISPLAY") != NULL )
		backend = WESTON_BACKEND_WAYLAND;
	else if ( getenv("DISPLAY") != NULL )
		backend = WESTON_BACKEND_X11;
	else
		backend = WESTON_BACKEND_DRM;

	switch (backend) {
	case WESTON_BACKEND_DRM:
		b->backend_config.drm.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
		b->backend_config.drm.base.struct_size = sizeof(struct weston_drm_backend_config);
		break;
	case WESTON_BACKEND_WAYLAND:
		b->backend_config.wayland.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
		b->backend_config.wayland.base.struct_size = sizeof(struct weston_wayland_backend_config);
		break;
	case WESTON_BACKEND_X11:
		b->backend_config.x11.base.struct_version = WESTON_X11_BACKEND_CONFIG_VERSION;
		b->backend_config.x11.base.struct_size = sizeof(struct weston_x11_backend_config);
		break;
	default:
		//not supported
		return  false;
	}
	weston_compositor_load_backend(compositor, backend, &b->backend_config.drm.base);
	//now the api table at compositor should be full now
	switch (backend) {
	case WESTON_BACKEND_DRM:
		//we probably need to do set mode or something
		b->api.drm = weston_drm_output_get_api(compositor);
		b->output_pending_listener.notify = output_pending_drm;
		break;
	case WESTON_BACKEND_WAYLAND:
	case WESTON_BACKEND_X11:
		b->api.window = weston_windowed_output_get_api(compositor);
		b->api.window->output_create(compositor, "taiwins");
		b->output_pending_listener.notify = output_pending_windowed;
		break;
	default:
		break;
	}
	wl_signal_add(&compositor->output_pending_signal, &b->output_pending_listener);
	weston_pending_output_coldplug(compositor);
	compositor->vt_switching = 1;

	//okay, create the cursor and background so we can show something
//	weston_layer_init(&b->compositor->cursor_layer, b->compositor);
//	weston_layer_set_position(&b->compositor->cursor_layer, WESTON_LAYER_POSITION_CURSOR);
	//after we create a cursor, we need to get the surface
//	weston_surface_
	weston_layer_init(&b->layer_background, b->compositor);
	weston_layer_set_position(&b->layer_background, WESTON_LAYER_POSITION_BACKGROUND);
	//I guess you will need to create a surface for cursor as well

	b->surface_background = weston_surface_create(b->compositor);
	weston_surface_set_size(b->surface_background, 1000, 1000);
	weston_surface_set_color(b->surface_background, 0.0f, 0.24f, 0.5f, 1.0f);
	b->view_background = weston_view_create(b->surface_background);
	weston_layer_entry_insert(&b->layer_background.view_list, &b->view_background->layer_link);



	//TODO: clean, tmp code

	return true;
}
