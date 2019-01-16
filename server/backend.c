#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <wayland-server.h>
#include <helpers.h>

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
	struct wl_listener heads_changed_listener;
	struct wl_listener output_pending_listener;
	struct weston_layer layer_background;
	struct weston_surface *surface_background;
	struct weston_view *view_background;
};

static struct tw_backend TWbackend;


static void
drm_head_enable(struct weston_head *head, struct weston_compositor *compositor)
{

	const struct weston_drm_output_api *api =
		weston_drm_output_get_api(compositor);

	//you can convert an output to pending state.
	//try to create the output for it, right now we need to be silly, just
	//use the clone method
	struct weston_output *output = wl_list_length(&compositor->output_list) ?
		container_of(compositor->output_list.next, struct weston_output, link) :
		NULL;
	if (!output)
		output = weston_compositor_create_output_with_head(compositor, head);
	else
		weston_output_attach_head(output, head);

	api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
	api->set_gbm_format(output, NULL);
	api->set_seat(output, NULL);
	weston_output_set_scale(output, 1);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_enable(output);

	if (!output->enabled) {
		weston_output_enable(output);
	}
}

static void
drm_head_disable(struct weston_head *head)
{
	struct weston_output *output = weston_head_get_output(head);
	weston_head_detach(head);
	if (wl_list_length(&output->head_list) == 0)
		weston_output_destroy(output);
}

static void
drm_head_check(struct weston_compositor *compositor)
{
	if (!wl_list_length(&compositor->output_list)) {
		struct weston_output *output =
			weston_compositor_create_output(compositor, "taiwins");
		wl_list_remove(&output->link);
		wl_list_insert(&compositor->output_list, &output->link);
	}
}


static void
drm_head_changed(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct weston_head *head = NULL;
	bool connected, enabled, changed;

	while ((head = weston_compositor_iterate_heads(compositor, head))) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		//shit, it is not connected or enabled
		if (connected && !enabled)
			drm_head_enable(head, compositor);
		else if (enabled && !connected)
			drm_head_disable(head);
		else {
		}
		weston_head_reset_device_changed(head);
	}
	drm_head_check(compositor);
}

static void
windowed_head_enable(struct weston_head *head, struct weston_compositor *compositor)
{
	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_move(output, 0, 0);
	weston_output_set_scale(output, 1);

	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);
	api->output_set_size(output, 1000, 1000);
	if (!output->enabled)
		weston_output_enable(output);
}

//we should have a weston_head_destroy_signal
static void
windowed_head_disabled(struct weston_head *head)
{
	struct weston_output *output = weston_head_get_output(head);
	weston_head_detach(head);
	weston_output_destroy(output);
}

static void
windowed_head_check(struct weston_compositor *compositor)
{
	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);
	if (!wl_list_length(&compositor->output_list)) {
		api->create_head(compositor, "windows");
	}
}

static void
windowed_head_changed(struct wl_listener *listener, void *data)
{
	//one head one output
	struct weston_compositor *compositor = data;
	struct weston_head *head = NULL;
	bool connected, enabled, changed;

	wl_list_for_each(head, &compositor->head_list, compositor_link) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		if (connected && !enabled) {
			windowed_head_enable(head, compositor);
		} else if (enabled && !connected) {
			windowed_head_disabled(head);
		} else if (enabled && changed) {
			//get the window info and... maybe resize.
		}
		weston_head_reset_device_changed(head);
	}

}

static struct wl_listener windowed_head_change_handler = {
	.notify = windowed_head_changed,
	.link.prev = &windowed_head_change_handler.link,
	.link.next = &windowed_head_change_handler.link,
};


static struct wl_listener drm_head_change_handler = {
	.notify = drm_head_changed,
	.link.prev = &drm_head_change_handler.link,
	.link.next = &drm_head_change_handler.link,
};

bool
tw_setup_backend(struct weston_compositor *compositor)
{
	enum weston_compositor_backend backend;
	struct tw_backend *b = weston_compositor_get_user_data(compositor);
	b->compositor = compositor;
	//apparently I need to do this
	struct xkb_context *ctxt = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	compositor->xkb_context = ctxt;

	if ( getenv("WAYLAND_DISPLAY") != NULL )
		backend = WESTON_BACKEND_WAYLAND;
	else if ( getenv("DISPLAY") != NULL )
		backend = WESTON_BACKEND_X11;
	else
		backend = WESTON_BACKEND_DRM;
	//set up a env so we can use headless backend

	switch (backend) {
	case WESTON_BACKEND_DRM:
		b->backend_config.drm.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
		b->backend_config.drm.base.struct_size = sizeof(struct weston_drm_backend_config);
		weston_compositor_add_heads_changed_listener(compositor, &drm_head_change_handler);
		//you need to have setup the drm backends as well
		break;
	case WESTON_BACKEND_WAYLAND:
		b->backend_config.wayland.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
		b->backend_config.wayland.base.struct_size = sizeof(struct weston_wayland_backend_config);
		weston_compositor_add_heads_changed_listener(compositor, &windowed_head_change_handler);
		break;
	case WESTON_BACKEND_X11:
		b->backend_config.x11.base.struct_version = WESTON_X11_BACKEND_CONFIG_VERSION;
		b->backend_config.x11.base.struct_size = sizeof(struct weston_x11_backend_config);
		weston_compositor_add_heads_changed_listener(compositor, &windowed_head_change_handler);
		break;
	default:
		//not supported
		return  false;
	}
	weston_compositor_load_backend(compositor, backend, &b->backend_config.drm.base);
	if (backend == WESTON_BACKEND_WAYLAND ||
	    backend == WESTON_BACKEND_X11)
		windowed_head_check(compositor);
	weston_compositor_flush_heads_changed(compositor);

	/* weston_log("we have heads now\n"); */
	compositor->vt_switching = 1;
	return true;
}

struct tw_backend *tw_get_backend(void)
{
	return &TWbackend;
}
