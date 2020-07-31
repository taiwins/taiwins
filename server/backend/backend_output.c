/*
 * backend_output.c - tw_backend output functions
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

#include <ctypes/helpers.h>
#include <wayland-server.h>

#include "backend.h"
#include "backend_internal.h"
#include <taiwins/objects/utils.h>
#include <taiwins/objects/matrix.h>
#include "renderer/renderer.h"

static enum wl_output_transform
inverse_wl_transform(enum wl_output_transform t)
{
	if ((t & WL_OUTPUT_TRANSFORM_90) &&
	    !(t & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		t ^= WL_OUTPUT_TRANSFORM_180;
	}
	return t;
}

//called when output is dirty
static void
build_output_2d_view(struct tw_backend_output *o)
{
	struct tw_mat3 glproj, tmp;
	int width, height;

	//this essentially map-backs a virtual space back to physical space.
	tw_mat3_init(&glproj);
	glproj.d[4] = -1;
	glproj.d[7] = o->state.h;
	wlr_output_transformed_resolution(o->wlr_output, &width, &height);

	//the transform should be
	// T' = glproj * inv_wl_transform * scale * -translate * T
	tw_mat3_translate(&o->state.view_2d, -o->state.x, -o->state.y);
	tw_mat3_transform_rect(&tmp, false,
	                       inverse_wl_transform(o->state.transform),
	                       width, height, o->state.scale);
	tw_mat3_multiply(&o->state.view_2d, &tmp, &o->state.view_2d);
	tw_mat3_multiply(&o->state.view_2d, &glproj, &o->state.view_2d);
}

static struct wlr_output_mode *
pick_output_mode(struct tw_backend_output *o, struct wlr_output *output)
{
	int32_t min_refresh = -1;
	struct wlr_output_mode *mode = NULL, *candidate = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == o->state.w &&
		    mode->height == o->state.h &&
		    mode->refresh == o->state.refresh)
			return mode;
		//if we have multiple mode with same resolution, pick the one
		//with highest refresh rate.
		else if (mode->width == o->state.w &&
		         mode->height == o->state.h &&
		         mode->refresh > min_refresh)
			candidate = mode;
	}
	if (candidate)
		return candidate;
	else
		return wlr_output_preferred_mode(output);
}

static inline void
correct_output_mode(struct tw_backend_output *o)
{
	struct wlr_output *wlr_output = o->wlr_output;
	o->state.w = wlr_output->width;
	o->state.h = wlr_output->height;
	o->state.refresh = wlr_output->refresh;
	pixman_region32_clear(&o->state.constrain.region);
	pixman_region32_init_rect(&o->state.constrain.region,
	                          o->state.x, o->state.y,
	                          o->state.w, o->state.h);
}
void
tw_backend_commit_output_state(struct tw_backend_output *o)
{
	struct wlr_output *output = o->wlr_output;
	struct wlr_output_mode *mode;

	if (o->state.dirty) {
		wlr_output_enable(output, o->state.activate);
		wlr_output_set_transform(output, o->state.transform);
		wlr_output_set_scale(output, o->state.scale);

		mode = (o->state.preferred_mode) ?
			wlr_output_preferred_mode(output) :
			pick_output_mode(o, output);
		wlr_output_set_mode(output, mode);
		//wlr_output_commit will call impl->commit which in turns would
		//update_the_output mode, x11 and wayland backend does not have
		//commit so output->mode would not change.
		wlr_output_commit(output);
		correct_output_mode(o);
		//build output transformation matrix
		build_output_2d_view(o);
		o->state.dirty = false;

		//now here we can decide if we want to implement
		//wlr_output_management protocol
	}
}

static void
init_output_state(struct tw_backend_output *o)
{
	o->state.dirty = true;
	o->state.activate = true;
	o->state.scale = 1.0;
	o->state.transform = WL_OUTPUT_TRANSFORM_NORMAL;
	o->state.preferred_mode = true;
	//okay, here is what we will need to fix
	o->state.x = 0; o->state.y = 0;
	wl_list_init(&o->state.constrain.link);
	pixman_region32_init(&o->state.constrain.region);
	wl_list_insert(o->backend->global_cursor.constrains.prev,
	               &o->state.constrain.link);
}

static void
notify_new_output_frame(struct wl_listener *listener, void *data)
{
	struct tw_backend_output *output =
		container_of(listener, struct tw_backend_output,
		             frame_listener);
	struct tw_backend *backend = output->backend;
	struct tw_renderer *renderer =
		container_of(backend->main_renderer, struct tw_renderer, base);

	if (output->state.repaint_state != TW_REPAINT_DIRTY)
		return;
	//output need to have transform

	if (!wlr_output_attach_render(output->wlr_output, NULL))
		return;

	renderer->repaint_output(renderer, output);
	wlr_output_commit(output->wlr_output);

	//sure this is the good place to start?
	wl_signal_emit(&backend->output_frame_signal, output);

	//clean off the repaint state
	output->state.repaint_state = TW_REPAINT_CLEAN;
}

static void
notify_output_remove(struct wl_listener *listener, UNUSED_ARG(void *data))
{
	struct tw_backend_output *output =
		container_of(listener, struct tw_backend_output,
		             destroy_listener);
	struct tw_backend *backend = output->backend;
	/* struct wlr_backend *wlr_backend = backend->auto_backend; */
	uint32_t unset = ~(1 << output->id);

	output->id = -1;
	wl_list_remove(&output->link);
	wl_list_remove(&output->state.constrain.link);
	pixman_region32_fini(&output->state.constrain.region);

	backend->output_pool &= unset;
	wl_signal_emit(&backend->output_unplug_signal, output);

	wlr_output_destroy_global(output->wlr_output);
	//TODO if is windowed output, we shall just quit, it can be done as a
	//signal as well.
}

void
tw_backend_new_output(struct tw_backend *backend,
                      struct wlr_output *wlr_output)
{
	uint32_t id = ffs(backend->output_pool);
	struct tw_backend_output *output = &backend->outputs[id];

	wlr_output->data = output;
	output->id = id;
	output->cloning = -1;
	output->backend = backend;
	output->wlr_output = wlr_output;
	output->state.repaint_state = TW_REPAINT_DIRTY;


	wl_list_init(&output->views);
	tw_signal_setup_listener(&wlr_output->events.frame,
	                         &output->frame_listener,
	                         notify_new_output_frame);
	tw_signal_setup_listener(&wlr_output->events.destroy,
	                         &output->destroy_listener,
	                         notify_output_remove);
	wlr_output_create_global(wlr_output);

	//setup default state
	init_output_state(output);

	//setup on backend side
        backend->output_pool |= 1 << id;
	wl_list_init(&output->link);
	if (backend->defer_output_creation)
		wl_list_insert(backend->pending_heads.prev, &output->link);
	else
		wl_list_insert(backend->heads.prev, &output->link);

	//tell the clients
        if (!backend->defer_output_creation) {
	        tw_backend_commit_output_state(output);
	        wl_signal_emit(&backend->output_plug_signal, output);
        }

}

struct tw_backend_output *
tw_backend_find_output(struct tw_backend *backend, const char *name)
{
	struct tw_backend_output *output = NULL;

	wl_list_for_each(output, &backend->heads, link) {
		if (strncasecmp(output->wlr_output->name, name, 32) == 0)
			return output;
	}
	wl_list_for_each(output, &backend->pending_heads, link) {
		if (strncasecmp(output->wlr_output->name, name, 32) == 0)
			return output;
	}
	return output;
}

void
tw_backend_set_output_scale(struct tw_backend_output *output, float scale)
{
	if (scale > 0.5 && scale < 4.0) {
		output->state.scale = scale;
		output->state.dirty = true;
	}
}

void
tw_backend_set_output_transformation(struct tw_backend_output *output,
                                     enum wl_output_transform transform)
{
	output->state.transform = transform;
	output->state.dirty = true;
}

int
tw_backend_get_output_modes(struct tw_backend_output *output,
                            struct tw_backend_output_mode *modes)
{
	struct wlr_output_mode *mode;
	int i = 0;
	int n_modes = wl_list_length(&output->wlr_output->modes);

	if (modes) {
		wl_list_for_each(mode, &output->wlr_output->modes, link) {
			modes[i].w = mode->width;
			modes[i].h = mode->height;
			modes[i].refresh = mode->refresh;
			i++;
		}
	}
	return n_modes;
}

void
tw_backend_set_output_mode(struct tw_backend_output *output,
                           const struct tw_backend_output_mode *mode)
{
	output->state.w = mode->w;
	output->state.h = mode->h;
	output->state.refresh = mode->refresh;
	output->state.dirty = true;
}

void
tw_backend_set_output_position(struct tw_backend_output *output,
                               uint32_t x, uint32_t y)
{
	output->state.x = x;
	output->state.y = y;
	output->state.dirty = true;
}

void
tw_backend_output_clone(struct tw_backend_output *dst,
                        const struct tw_backend_output *src)
{
	//TODO there are a few problems with this, this method will not work
	//well.

	//1. If src changes after this function, do we change the dst as well?

        //2. If src was destroyed, how do we deal with the clone?

	//3. What if there is a cascading setup?
	dst->cloning = src->id;
	dst->state.x = src->state.x;
	dst->state.y = src->state.x;
	dst->state.w = src->state.w;
	dst->state.h = src->state.h;
	dst->state.refresh = src->state.refresh;
	dst->state.scale = src->state.scale;
	dst->state.transform = src->state.transform;
	dst->state.preferred_mode = src->state.preferred_mode;
	dst->state.gamma_value = src->state.gamma_value;
	dst->state.activate = src->state.activate;

	dst->state.dirty = true;
}

void
tw_backend_output_enable(struct tw_backend_output *output,
                         bool enable)
{
	output->state.activate = enable;
	output->state.dirty = true;
}

//gamma or color temperature ?
void
tw_backend_output_set_gamma(struct tw_backend_output *output,
                            float gamma)
{
	if (gamma > 0.5 && gamma < 2.2)
		output->state.gamma_value = gamma;
	//TODO: having gamma with effect also
	//output->state.dirty = true;
}

void
tw_backend_output_dirty(struct tw_backend_output *output)
{
	if (output->state.repaint_state != TW_REPAINT_CLEAN)
		return;
	output->state.repaint_state = TW_REPAINT_DIRTY;
}
