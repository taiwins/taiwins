/*
 * layer_render.c - taiwins backend layer renderer functions
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
#include <wayland-util.h>

#include <taiwins/objects/surface.h>
#include "backend.h"
#include <renderer/renderer.h>

#include "pixman.h"
#include "taiwins.h"


static void
surface_add_to_output_list(struct tw_server *server,
                           struct tw_surface *surface)
{
	struct tw_backend_output *output;
	struct tw_subsurface *sub;

	assert(surface->output >= 0 && surface->output <= 31);
	output = &server->backend->outputs[surface->output];

	wl_list_insert(output->views.prev,
	               &surface->links[TW_VIEW_OUTPUT_LINK]);

	wl_list_for_each(sub, &surface->subsurfaces, parent_link)
		surface_add_to_output_list(server, sub->surface);
}

/* subsurface is not intented for widget, tooltip and all that. It is for gluing
 * multiple videos together to a bigger window, if this is the case, the
 * tw_surface would have to a bit different. */

static void
surface_add_to_list(struct tw_server *server, struct tw_surface *surface)
{
	//we should also add to the output
	struct tw_subsurface *sub;
	struct tw_layers_manager *manager = &server->backend->layers_manager;

	wl_list_insert(manager->views.prev,
	               &surface->links[TW_VIEW_GLOBAL_LINK]);

	//subsurface inserts just above its main surface, here we take the
	//reverse order of the subsurfaces and insert them one by one in front
	//of the main surface
	wl_list_for_each_reverse(sub, &surface->subsurfaces, parent_link) {
		wl_list_insert(surface->links[TW_VIEW_GLOBAL_LINK].prev,
		               &sub->surface->links[TW_VIEW_GLOBAL_LINK]);
	}
}

void
tw_server_build_surface_list(struct tw_server *server)
{
	struct tw_surface *surface;
	struct tw_layer *layer;
	struct tw_layers_manager *manager = &server->backend->layers_manager;
	struct tw_backend_output *output;

	wl_list_init(&manager->views);
	wl_list_for_each(output, &server->backend->heads, link)
		wl_list_init(&output->views);

	wl_list_for_each(layer, &manager->layers, link) {
		wl_list_for_each(surface, &layer->views,
		                 links[TW_VIEW_LAYER_LINK]) {
			surface_add_to_list(server, surface);
			surface_add_to_output_list(server, surface);
		}
	}
}

// from this function we know that we are missing a few pieces.
// can we compute the surface->clip here?
// if we take this approach, surface would not be able to
static void
surface_accumulate_damage(struct tw_surface *surface,
                          pixman_region32_t *clipped,
                          pixman_region32_t *accum_damage)
{
	pixman_region32_t damage, bbox, old_bbox;

	pixman_region32_init(&damage);
	pixman_region32_init_rect(&bbox,
	                          surface->geometry.xywh.x,
	                          surface->geometry.xywh.y,
	                          surface->geometry.xywh.width,
	                          surface->geometry.xywh.height);
	pixman_region32_init_rect(&old_bbox,
	                          surface->geometry.prev_xywh.x,
	                          surface->geometry.prev_xywh.y,
	                          surface->geometry.prev_xywh.width,
	                          surface->geometry.prev_xywh.height);

	if (surface->geometry.dirty) {
		pixman_region32_union(&damage, &bbox, &old_bbox);
	} else {
		//TODO, here we use surface damage, surface actually relies on
		//buffer damage
		pixman_region32_copy(&damage, &surface->current->surface_damage);
		pixman_region32_translate(&damage, surface->geometry.xywh.x,
		                          surface->geometry.xywh.y);
	}
	pixman_region32_intersect(&damage, &damage, &bbox);
	pixman_region32_subtract(&damage, &damage, clipped);
	pixman_region32_union(accum_damage, accum_damage, &damage);
	//TODO: union the total damage with our surface damage.

	//update the clip region here. but yeah, our surface region is not
	//correct at all.
	pixman_region32_copy(&surface->clip, clipped);
	pixman_region32_union(clipped, clipped,
	                      &surface->current->opaque_region);

	pixman_region32_fini(&damage);
	pixman_region32_fini(&bbox);
	pixman_region32_fini(&old_bbox);
}

void
tw_server_stack_damage(struct tw_server *server)
{
	struct tw_surface *surface;
	struct tw_backend_output *output;

	//the clip the total coverred region, opaque is the per-plane covered
	//region. Well we only have one plane.
	pixman_region32_t opaque, clip, total_damage;

	pixman_region32_init(&clip);
	pixman_region32_init(&total_damage);
	wl_list_for_each(output, &server->backend->heads, link) {
		pixman_region32_init(&opaque);

		wl_list_for_each(surface, &output->views,
		                 links[TW_VIEW_OUTPUT_LINK]) {
			surface_accumulate_damage(surface, &opaque,
			                          &total_damage);
		}
		pixman_region32_union(&clip, &clip, &opaque);

		pixman_region32_fini(&opaque);
	}
	pixman_region32_fini(&clip);
}

void
tw_renderer_render_layers(struct wlr_renderer *renderer,
                          struct tw_backend_output *output,
                          struct tw_layers_manager *layers)
{
	// looking at the libweston code, repainting is triggered by timer, by
	// checking if the output needs to repaint( surface is marked dirty ).
	// if we do need to repaint:

	// 1) building a view list for the compositor(or surface list).

	// 2) predraw: Calculating the output damage (cursor
	// can assign to a plane) and taking the surface damage and frame
	// callbacks. at the same time(no surface damage).

	// 3) calling output->repaint with the damage, this would be actually
	// drawing textures on the output.

	// 4) postdraw: release the frame-callbacks, presentation feedbacks,
	// resetting states for server.

	//weston did it in a monolith way, explicitly calling everything, we
	//would want to avoid that, since protocols like wp_viewporter,
	//presentation_feedback are optional, they would have to go into a
	//listener.

	//1st thing is building view list.
	//2nd. move to planes, when you move the views to the plane,
	//view->clip is the region that covers the view.

}
