/*
 * layer_renderer.c - an layer renderer implementation
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

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <pixman.h>
#include <ctypes/helpers.h>

#include <taiwins/objects/layers.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/matrix.h>
#include <taiwins/objects/plane.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/profiler.h>
#include "renderer.h"
#include "shaders.h"
#include "backend.h"

struct tw_layer_renderer {
	struct tw_renderer base;
	struct tw_quad_tex_shader quad_shader;
	/* for external sampler */
	struct tw_quad_tex_shader ext_quad_shader;
	struct wl_listener destroy_listener;
};


static void
surface_add_to_outputs_list(struct tw_backend *backend,
                           struct tw_surface *surface)
{
	struct tw_backend_output *output;
	struct tw_subsurface *sub;

	assert(surface->output >= 0 && surface->output <= 31);
	output = &backend->outputs[surface->output];

	wl_list_insert(output->views.prev,
	               &surface->links[TW_VIEW_OUTPUT_LINK]);

	wl_list_for_each(sub, &surface->subsurfaces, parent_link)
		surface_add_to_outputs_list(backend, sub->surface);
}

/* subsurface is not intented for widget, tooltip and all that. It is for gluing
 * multiple videos together to a bigger window, if this is the case, the
 * tw_surface would have to a bit different. */

static void
surface_add_to_list(struct tw_backend *backend, struct tw_surface *surface)
{
	//we should also add to the output
	struct tw_subsurface *sub;
	struct tw_layers_manager *manager = &backend->layers_manager;

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

static void
tw_layer_renderer_build_surface_list(struct tw_backend *backend)
{
	struct tw_surface *surface;
	struct tw_layer *layer;
	struct tw_backend_output *output;
	struct tw_layers_manager *manager = &backend->layers_manager;

	SCOPE_PROFILE_BEG();

	wl_list_init(&manager->views);
	wl_list_for_each(output, &backend->heads, link)
		wl_list_init(&output->views);

	wl_list_for_each(layer, &manager->layers, link) {
		wl_list_for_each(surface, &layer->views,
		                 links[TW_VIEW_LAYER_LINK]) {
			surface_add_to_list(backend, surface);
			surface_add_to_outputs_list(backend, surface);
		}
	}

	SCOPE_PROFILE_END();
}

static void
surface_accumulate_damage(struct tw_surface *surface,
                          pixman_region32_t *clipped)
{
	pixman_region32_t damage, bbox, old_bbox;
	struct tw_view *current = surface->current;

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
		pixman_region32_copy(&damage, &current->surface_damage);
		pixman_region32_translate(&damage, surface->geometry.xywh.x,
		                          surface->geometry.xywh.y);
	}
	pixman_region32_intersect(&damage, &damage, &bbox);
	pixman_region32_subtract(&damage, &damage, clipped);
	pixman_region32_union(&current->plane->damage,
	                      &current->plane->damage, &damage);

	//update the clip region here. but yeah, our surface region is not
	//correct at all.
	pixman_region32_copy(&surface->clip, clipped);
	pixman_region32_union(clipped, clipped,
	                      &current->opaque_region);

	pixman_region32_fini(&damage);
	pixman_region32_fini(&bbox);
	pixman_region32_fini(&old_bbox);
}

static void
tw_layer_renderer_stack_damage(struct tw_backend *backend)
{
	struct tw_surface *surface;
	struct tw_backend_output *output;

	//the clip the total coverred region, opaque is the per-plane covered
	//region. Well we only have one plane.
	pixman_region32_t opaque, clip;

	SCOPE_PROFILE_BEG();

	pixman_region32_init(&clip);
	wl_list_for_each(output, &backend->heads, link) {
		pixman_region32_init(&opaque);

		wl_list_for_each(surface, &output->views,
		                 links[TW_VIEW_OUTPUT_LINK]) {
			surface_accumulate_damage(surface, &opaque);
		}
		pixman_region32_union(&clip, &clip, &opaque);

		pixman_region32_fini(&opaque);
	}
	pixman_region32_fini(&clip);

	SCOPE_PROFILE_END();
}

/******************************************************************************
 * actual painting
 * even libweston is doing better than this.
 *****************************************************************************/
static void
layer_renderer_draw_quad(bool y_inverted)
{
	////////////////////////////////
	//
	//      1 <-------- 0
	//        | \     |
	//        |   \   |
	//        |    \  |
	//        |     \ |
	//      3 --------- 2
	//
	////////////////////////////////
	GLfloat verts[] = {
		1, -1,
		-1, -1,
		1, 1,
		-1, 1,
	};
	// tex coordinates, OpenGL stores texture upside down, y_inverted here
	// means the texture follows OpenGL
	GLfloat texcoords_y_inverted[] = {
		1, 1,
		0, 1,
		1, 0,
		0, 0
	};
	GLfloat texcoords[] = {
		1, 0,
		0, 0,
		1, 1,
		0, 1
	};

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0,
	                      y_inverted ? texcoords_y_inverted : texcoords);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void
layer_renderer_cleanup_buffer(struct tw_renderer *renderer,
                              struct tw_backend_output *output)
{
	//how much GL information you want
	glViewport(0, 0, output->state.w, output->state.h);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	renderer->viewport_h = output->state.h;
	renderer->viewport_h = output->state.w;

	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
}

static void
layer_renderer_paint_surface(struct tw_surface *surface,
                             struct tw_layer_renderer *rdr,
                             struct tw_backend_output *o)
{
	struct tw_mat3 proj, tmp;
	struct tw_quad_tex_shader *shader;
	struct tw_render_texture *texture = surface->buffer.handle.ptr;

	SCOPE_PROFILE_BEG();

	if (!texture)
		return;

	switch (texture->target) {
	case GL_TEXTURE_2D:
		shader = &rdr->quad_shader;
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		shader = &rdr->ext_quad_shader;
		break;
	default:
		tw_logl_level(TW_LOG_ERRO, "unknown texture format!");
		return;
	}

	tw_mat3_multiply(&tmp,
	                 &o->state.view_2d,
	                 &surface->geometry.transform);
	tw_mat3_ortho_proj(&proj, o->state.w, o->state.h);
	tw_mat3_multiply(&proj, &proj, &tmp);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->gltex);
	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUseProgram(shader->prog);
	glUniformMatrix3fv(shader->uniform.proj, 1, GL_FALSE, proj.d);
	glUniform1i(shader->uniform.texture, 0);
	glUniform1f(shader->uniform.alpha, 1.0f);

	layer_renderer_draw_quad(texture->inverted_y);

	SCOPE_PROFILE_END();
}

static void
layer_renderer_repaint_output(struct tw_renderer *renderer,
                              struct tw_backend_output *output)
{
	struct tw_plane main_plane;
	struct tw_surface *surface;
	struct tw_backend *backend = output->backend;
	struct tw_layers_manager *manager = &backend->layers_manager;
	struct tw_layer_renderer *layer_render =
		container_of(renderer, struct tw_layer_renderer, base);
	struct timespec now;
	uint32_t now_int;

	SCOPE_PROFILE_BEG();

	tw_layer_renderer_build_surface_list(backend);
	tw_plane_init(&main_plane);

	//move to plane
	wl_list_for_each(surface, &manager->views,
	                 links[TW_VIEW_GLOBAL_LINK]) {
		surface->current->plane = &main_plane;
	}
	tw_layer_renderer_stack_damage(backend);

	layer_renderer_cleanup_buffer(renderer, output);

	wl_list_for_each_reverse(surface, &manager->views,
	                         links[TW_VIEW_GLOBAL_LINK]) {

		layer_renderer_paint_surface(surface, layer_render, output);

		clock_gettime(CLOCK_MONOTONIC, &now);
		now_int = now.tv_sec * 1000 + now.tv_nsec / 1000000;
		tw_surface_flush_frame(surface, now_int);
	}
	tw_plane_fini(&main_plane);

	SCOPE_PROFILE_END();
}


static void
tw_layer_renderer_destroy(struct wlr_renderer *wlr_renderer)
{
	struct tw_layer_renderer *renderer =
		container_of(wlr_renderer, struct tw_layer_renderer,
		             base.base);
	tw_quad_tex_blend_shader_fini(&renderer->quad_shader);
	tw_quad_tex_ext_blend_shader_fini(&renderer->quad_shader);
	tw_renderer_base_fini(&renderer->base);
	free(renderer);
}

struct wlr_renderer *
tw_layer_renderer_create(struct wlr_egl *egl, EGLenum platform,
                         void *remote_display, EGLint *config_attribs,
                         EGLint visual_id)
{
	struct tw_layer_renderer *renderer =
		calloc(1, sizeof(struct tw_layer_renderer));
	if (!renderer)
		return NULL;
	if (!tw_renderer_init_base(&renderer->base, egl, platform,
	                           remote_display, visual_id)) {
		free(renderer);
		return NULL;
	}
	tw_quad_tex_blend_shader_init(&renderer->quad_shader);
	tw_quad_tex_ext_blend_shader_init(&renderer->ext_quad_shader);
	renderer->base.wlr_impl.destroy = tw_layer_renderer_destroy;
	wlr_renderer_init(&renderer->base.base, &renderer->base.wlr_impl);

	renderer->base.repaint_output = layer_renderer_repaint_output;

	return &renderer->base.base;
}
