/*
 * backend.c - taiwins backend functions
 *
 * Copyright (c) 2019 Xichen Zhou
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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
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
typedef OPTION(enum wl_output_transform, transform) tw_option_output_transform;

struct tw_output {
	int id;
	struct tw_backend *backend;
	struct weston_output *output;
	tw_option_geometry pending_geometry;
	tw_option_scale pending_scale;
	tw_option_output_transform pending_transform;
};

struct tw_backend {
	struct weston_compositor *compositor;
	struct taiwins_config *config;
	enum weston_compositor_backend type;
	//like weston, we have maximum 32 outputs.
	uint32_t output_pool;
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
static void
tw_backend_init_output(struct tw_backend *b, struct weston_output *output)
{
	assert(ffs(~b->output_pool) > 0);
	int id = ffs(~b->output_pool) - 1;
	b->output_pool |= 1u << id;
	b->outputs[id].id = id;
	b->outputs[id].output = output;
	b->outputs[id].backend = b;
	b->outputs[id].pending_geometry.valid = false;
	b->outputs[id].pending_scale.valid = false;
	b->outputs[id].pending_transform.valid = false;
}

static void
tw_backend_fini_output(struct tw_output *o)
{
	struct tw_backend *backend = o->backend;
	backend->output_pool &= ~(1u << o->id);
	o->id = 0xffffffff;
	o->output = NULL;
	o->pending_geometry.valid = false;
	o->pending_scale.valid = false;
	o->pending_transform.valid = false;
}

static inline struct tw_output*
tw_output_from_weston_output(struct weston_output *o, struct tw_backend *b)
{
	for (int i = 0; i < 32; i++)
		if (b->outputs[i].output == o)
			return &b->outputs[i];
	return NULL;
}


/******************************************************************
 * head functions
 *****************************************************************/
/* here we just create one output, every thing else attach to it, but later, we
 * can create other outputs */
static void
drm_head_enable(struct weston_head *head, struct weston_compositor *compositor,
		struct tw_backend *backend)
{
	const struct weston_drm_output_api *api =
		weston_drm_output_get_api(compositor);

	//you can convert an output to pending state.
	//try to create the output for it, right now we need to be silly, just
	//use the clone method
	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);

	api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
	api->set_gbm_format(output, NULL);
	api->set_seat(output, NULL);
	weston_output_set_scale(output, 1);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_enable(output);
	tw_backend_init_output(backend, output);

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
windowed_head_enable(struct weston_head *head, struct weston_compositor *compositor,
		     struct tw_backend *backend)
{
	const struct weston_windowed_output_api *api =
		weston_windowed_output_get_api(compositor);

	struct weston_output *output =
		weston_compositor_create_output_with_head(compositor, head);
	weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	weston_output_set_scale(output, 1);
	api->output_set_size(output, 500, 500);
	if (!output->enabled)
		weston_output_enable(output);
	tw_backend_init_output(backend, output);
}

static void
windowed_head_disabled(struct weston_head *head, struct tw_backend *backend)
{
	struct weston_output *output = weston_head_get_output(head);
	struct tw_output *o = tw_output_from_weston_output(output, backend);
	weston_head_detach(head);
	weston_output_destroy(output);
	tw_backend_fini_output(o);

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
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, drm_head_changed);
	struct weston_compositor *compositor = data;
	struct weston_head *head = NULL;
	bool connected, enabled, changed;

	while ((head = weston_compositor_iterate_heads(compositor, head))) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		//shit, it is not connected or enabled
		if (connected && !enabled)
			drm_head_enable(head, compositor, backend);
		else if (enabled && !connected)
			drm_head_disable(head);
		else {
		}
		(void) changed;
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
	struct tw_backend *backend =
		container_of(listener, struct tw_backend, windowed_head_changed);
	bool connected, enabled, changed;

	wl_list_for_each(head, &compositor->head_list, compositor_link) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);
		if (connected && !enabled) {
			windowed_head_enable(head, compositor, backend);
		} else if (enabled && !connected) {
			windowed_head_disabled(head, backend);
		} else if (enabled && changed) {
			//get the window info and... maybe resize.
		}
		weston_head_reset_device_changed(head);
	}

}

/************************************************************
 * config components
 ***********************************************************/
#define METATABLE_OUTPUT "metatable_output"
#define REGISTRY_BACKEND "__backend"

typedef struct { int rotate; bool flip; enum wl_output_transform t;} transform_t;
static transform_t TRANSFORMS[] = {
	{0, false, WL_OUTPUT_TRANSFORM_NORMAL},
	{90, false, WL_OUTPUT_TRANSFORM_90},
	{180, false, WL_OUTPUT_TRANSFORM_180},
	{270, false, WL_OUTPUT_TRANSFORM_270},
	{0, true, WL_OUTPUT_TRANSFORM_FLIPPED},
	{90, true, WL_OUTPUT_TRANSFORM_FLIPPED_90},
	{180, true, WL_OUTPUT_TRANSFORM_FLIPPED_180},
	{270, true, WL_OUTPUT_TRANSFORM_FLIPPED_270},
};

static inline struct tw_backend *
_lua_to_backend(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_BACKEND);
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
_lua_is_windowed_display(lua_State *L)
{
	struct tw_backend *b = _lua_to_backend(L);
	lua_pushboolean(L, (b->type == WESTON_BACKEND_X11 ||
			    b->type == WESTON_BACKEND_WAYLAND ||
			    b->type == WESTON_BACKEND_RDP ||
			    b->type == WESTON_BACKEND_HEADLESS));
	return 1;
}

static int
_lua_get_windowed_output(lua_State *L)
{
	struct tw_output *tw_output = NULL;
	struct tw_backend *backend = _lua_to_backend(L);
	//we create a copy of it
	if (!wl_list_length(&backend->compositor->output_list)) {
		return luaL_error(L, "no displays available");
	} else {
		struct weston_output *output =
			container_of(backend->compositor->output_list.next,
				     struct weston_output, link);
		tw_output = tw_output_from_weston_output(output, backend);
	}
	//we are using this only for weston_output
	struct tw_output *lua_output =
		lua_newuserdata(L, sizeof(struct tw_output));
	lua_output->output = tw_output->output;
	luaL_getmetatable(L, METATABLE_OUTPUT);
	lua_setmetatable(L, -2);
	return 1;
}

static inline enum wl_output_transform
_lua_output_transfrom_from_value(lua_State *L, int rotate, bool flip)
{
	for (int i = 0; i < NUMOF(TRANSFORMS); i++)
		if (TRANSFORMS[i].rotate == rotate && TRANSFORMS[i].flip == flip)
			return TRANSFORMS[i].t;
	return luaL_error(L, "invalid transforms option.");
}

static int
_lua_output_rotate_flip(lua_State *L)
{
	struct tw_backend *backend = _lua_to_backend(L);
	struct tw_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);
	struct tw_output *tw_output = tw_output_from_weston_output(
		lua_output->output, backend);

	if (lua_gettop(L) == 1) {
		transform_t transform =
			TRANSFORMS[lua_output->output->transform];
		lua_pushinteger(L, transform.rotate);
		lua_pushboolean(L, transform.flip);
		return 2;
	} else if(lua_gettop(L) == 2) {
		int rotate = luaL_checkinteger(L, 2);
		bool flip = false;
		tw_output->pending_transform.transform =
			_lua_output_transfrom_from_value(L, rotate, flip);
		tw_output->pending_transform.valid = true;
		return 0;
	} else if (lua_gettop(L) == 3) {
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		int rotate = luaL_checkinteger(L, 2);
		int flip = lua_toboolean(L, 3);
		tw_output->pending_transform.transform =
			_lua_output_transfrom_from_value(L, rotate, flip);
		tw_output->pending_transform.valid = true;
		return 0;
	} else
		return luaL_error(L, "invalid number of arguments");
}

static int
_lua_output_scale(lua_State *L)
{
	struct tw_backend *backend =
		_lua_to_backend(L);
	struct tw_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);
	struct tw_output *tw_output =
		tw_output_from_weston_output(lua_output->output,
						  backend);
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, lua_output->output->scale);
		return 1;
	} else {
		_lua_stackcheck(L, 2);
		int scale = luaL_checkinteger(L, 2);
		if (scale <= 0 || scale > 4)
			return luaL_error(L, "invalid display scale");
		tw_output->pending_scale.scale = scale;
		tw_output->pending_scale.valid = true;
		return 0;
	}
}

//weston provides method to setup resolution/refresh_rate/aspect_ratio.
//we just deal with resolution now
static int
_lua_output_resolution(lua_State *L)
{
	struct tw_output *lua_output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, lua_output->output->width);
		lua_pushinteger(L, lua_output->output->height);
		return 2;
	} else {
		//TODO we deal with THIS later
		_lua_stackcheck(L, 2);
		return 0;
	}
}

static int
_lua_output_position(lua_State *L)
{
	struct tw_output *output =
		luaL_checkudata(L, 1, METATABLE_OUTPUT);
	(void)output;

	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, output->output->x);
		lua_pushinteger(L, output->output->y);
		return 2;
	} else {
		//TODO we deal with this later.
		_lua_stackcheck(L, 2);
		return 0;
	}
}

//we also need to be able to clone someone

static void
backend_apply_lua_config(struct taiwins_config *c, bool cleanup,
			 struct taiwins_config_component_listener *listener)
{
	struct weston_output *o;
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    config_component);

	wl_list_for_each(o, &b->compositor->output_list, link) {
		struct tw_output *to =
			tw_output_from_weston_output(o, b);
		bool valid = to->pending_scale.valid ||
			to->pending_geometry.valid ||
			to->pending_transform.valid;
		if (cleanup) {
			to->pending_scale.valid = false;
			to->pending_geometry.valid = false;
			to->pending_transform.valid = false;
			break;
		}
		if (to->pending_scale.valid) {
			o->scale = to->pending_scale.scale;
			//weston prevents us from doing this
			/* weston_output_set_scale( */
			/*	o, to->pending_scale.scale); */
			to->pending_scale.valid = false;
		}
		if (to->pending_transform.valid) {
			weston_output_set_transform(
				o, to->pending_transform.transform);
			to->pending_transform.valid = false;
		}
		if (valid)
			wl_signal_emit(&b->compositor->output_resized_signal, o);
	}
	weston_compositor_schedule_repaint(b->compositor);
}

static bool
backend_init_config_component(struct taiwins_config *c, lua_State *L,
			      struct taiwins_config_component_listener *listener)
{
	struct tw_backend *b = container_of(listener, struct tw_backend,
					    config_component);
	lua_pushlightuserdata(L, b);
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_BACKEND);
	//metatable for output
	luaL_newmetatable(L, METATABLE_OUTPUT);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__newindex");
	//here we choose to make into functions so use output:flip(270) instead of output.flip = 270
	REGISTER_METHOD(L, "rotate_flip", _lua_output_rotate_flip);
	REGISTER_METHOD(L, "scale", _lua_output_scale);
	REGISTER_METHOD(L, "resolution", _lua_output_resolution);
	REGISTER_METHOD(L, "position", _lua_output_position);
	lua_pop(L, 1);

	//global methods
	REGISTER_METHOD(L, "is_windowed_display", _lua_is_windowed_display);
	REGISTER_METHOD(L, "is_under_x11", _lua_is_under_x11);
	REGISTER_METHOD(L, "is_under_wayland", _lua_is_under_wayland);
	//we are calling display instead of output
	REGISTER_METHOD(L, "get_window_display", _lua_get_windowed_output);

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
	compositor->vt_switching = true;
	b->config = config;
	b->compositor = compositor;
	b->output_pool = 0;
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
