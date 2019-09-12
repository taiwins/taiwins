#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <linux/input.h>
#include <pixman.h>
#include <wayland-server.h>
#include <helpers.h>

#define INCLUDE_BACKEND
#include "taiwins.h"
#include "config.h"

struct tw_backend;

typedef OPTION(struct weston_geometry, geometry) tw_option_geometry;
typedef OPTION(int32_t, scale) tw_option_scale;

struct tw_output {
	struct tw_backend *backend;
	struct weston_output *output;
	tw_option_geometry pending_geometry;
	tw_option_scale pending_scale;
};

struct tw_backend {
	struct weston_compositor *compositor;
	struct taiwins_config *config;
	enum weston_compositor_backend type;
	//like weston, we have maximum 32 outputs.
	uint32_t output_mask;
	struct tw_output outputs[32];

	struct pixman_region32 region;
	union {
		struct weston_drm_backend_config drm;
		struct weston_wayland_backend_config wayland;
		struct weston_x11_backend_config x11;
	} backend_config;
	struct wl_listener compositor_distroy_listener;
	struct wl_listener windowed_head_changed;
	struct wl_listener drm_head_changed;
	struct taiwins_config_component_listener config_component;
};

static struct tw_backend TWbackend;


/******************************************************************
 * tw_output
 *****************************************************************/

/******************************************************************
 * head functions
 *****************************************************************/
/* here we just create one output, every thing else attach to it, but later, we
 * can create other outputs */
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
windowed_head_enable(struct weston_head *head, struct weston_compositor *compositor)
{
	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_move(output, 0, 0);
	weston_output_set_scale(output, 2);

	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);
	api->output_set_size(output, 500, 500);
	if (!output->enabled)
		weston_output_enable(output);
}

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

/************************************************************
 * head_listener
 ***********************************************************/
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

/************************************************************
 * config components
 ***********************************************************/
static inline struct tw_backend *
_lua_to_backend(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__backend");
	struct tw_backend *b = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return b;
}

static int
_lua_is_under_x11(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	lua_pushboolean(L, (b->type == WESTON_BACKEND_X11));
	return 1;
}

static int
_lua_is_under_wayland(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	lua_pushboolean(L, (b->type == WESTON_BACKEND_WAYLAND));
	return 1;
}

static int
_lua_get_windowed_output(lua_State *L)
{
	lua_newtable(L);

	return 1;
}

static void
backend_apply_lua_config(struct taiwins_config *c, bool cleanup,
			 struct taiwins_config_component_listener *listener)
{
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    config_component);
	(void)b;
}

static bool
backend_init_config_component(struct taiwins_config *c, lua_State *L,
			      struct taiwins_config_component_listener *listener)
{
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    config_component);
	lua_pushlightuserdata(L, b);
	lua_setfield(L, LUA_REGISTRYINDEX, "__backend");
	REGISTER_METHOD(L, "is_under_x11", _lua_is_under_x11);
	REGISTER_METHOD(L, "is_under_wayland", _lua_is_under_wayland);
	REGISTER_METHOD(L, "get_windowed_output", _lua_get_windowed_output);

	(void)b;
	return true;
}

/************************************************************
 * global functions
 ***********************************************************/

static inline struct tw_backend *
get_backend(void)
{
	return &TWbackend;
}

static void
end_backends(struct wl_listener *listener, void *data)
{
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    compositor_distroy_listener);
	pixman_region32_fini(&b->region);
}

static void
setup_backend_listeners(struct tw_backend *b)
{
	struct weston_compositor *compositor = b->compositor;
	wl_list_init(&b->windowed_head_changed.link);
	b->windowed_head_changed.notify = windowed_head_changed;
	wl_list_init(&b->drm_head_changed.link);
	b->drm_head_changed.notify = drm_head_changed;
	//global destroy
	wl_list_init(&b->compositor_distroy_listener.link);
	b->compositor_distroy_listener.notify = end_backends;
	wl_signal_add(&b->compositor->destroy_signal,
		      &b->compositor_distroy_listener);
	wl_list_init(&b->config_component.link);
	b->config_component.init = backend_init_config_component;
	b->config_component.apply = backend_apply_lua_config;
	taiwins_config_add_component(b->config, &b->config_component);

	switch (b->type) {
	case WESTON_BACKEND_DRM:
		b->backend_config.drm.base.struct_version =
			WESTON_DRM_BACKEND_CONFIG_VERSION;
		b->backend_config.drm.base.struct_size =
			sizeof(struct weston_drm_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->drm_head_changed);
		break;
	case WESTON_BACKEND_WAYLAND:
		b->backend_config.wayland.base.struct_version =
			WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
		b->backend_config.wayland.base.struct_size =
			sizeof(struct weston_wayland_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->windowed_head_changed);
		break;
	case WESTON_BACKEND_X11:
		b->backend_config.x11.base.struct_version =
			WESTON_X11_BACKEND_CONFIG_VERSION;
		b->backend_config.x11.base.struct_size =
			sizeof(struct weston_x11_backend_config);
		weston_compositor_add_heads_changed_listener(
			compositor, &b->windowed_head_changed);
		break;
	/* case WESTON_BACKEND_RDP: */
	default:
		//not supported
		assert(0);
	}
}

bool
tw_setup_backend(struct weston_compositor *compositor,
		 struct taiwins_config *config)
{
	enum weston_compositor_backend backend;
	struct tw_backend *b = get_backend();
	b->config = config;
	b->compositor = compositor;
	compositor->vt_switching = true;
	//how to launch an rdp server here?
	if ( getenv("WAYLAND_DISPLAY") != NULL )
		backend = WESTON_BACKEND_WAYLAND;
	else if ( getenv("DISPLAY") != NULL )
		backend = WESTON_BACKEND_X11;
	else
		backend = WESTON_BACKEND_DRM;
	b->type = backend;
	setup_backend_listeners(b);

	weston_compositor_load_backend(
		compositor, backend, &b->backend_config.drm.base);
	if (backend == WESTON_BACKEND_WAYLAND ||
	    backend == WESTON_BACKEND_X11)
		windowed_head_check(compositor);
	weston_compositor_flush_heads_changed(compositor);

	return true;
}
