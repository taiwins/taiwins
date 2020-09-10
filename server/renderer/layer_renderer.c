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
#include <time.h>
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
	struct tw_plane main_plane;

	struct tw_quad_tex_shader quad_shader;
	/* for debug rendering */
	struct tw_quad_color_shader color_quad_shader;
	/* for external sampler */
	struct tw_quad_tex_shader ext_quad_shader;
	struct wl_listener destroy_listener;
};

static void
surface_accumulate_damage(struct tw_surface *surface,
                          pixman_region32_t *clipped)
{
	pixman_region32_t damage, bbox, opaque;
	struct tw_view *current = surface->current;

	pixman_region32_init(&damage);
	pixman_region32_init_rect(&bbox,
	                          surface->geometry.xywh.x,
	                          surface->geometry.xywh.y,
	                          surface->geometry.xywh.width,
	                          surface->geometry.xywh.height);
	pixman_region32_init(&opaque);

	if (pixman_region32_not_empty(&surface->geometry.dirty)) {
		pixman_region32_copy(&damage, &surface->geometry.dirty);
	} else {
		pixman_region32_copy(&damage, &current->surface_damage);
		pixman_region32_translate(&damage, surface->geometry.xywh.x,
		                          surface->geometry.xywh.y);
		pixman_region32_intersect(&damage, &damage, &bbox);
	}
	pixman_region32_subtract(&damage, &damage, clipped);
	pixman_region32_union(&current->plane->damage,
	                      &current->plane->damage, &damage);
	//update the clip region here. but yeah, our surface region is not
	//correct at all.
	pixman_region32_subtract(&surface->clip, &bbox, clipped);
	pixman_region32_copy(&opaque, &current->opaque_region);
	pixman_region32_translate(&opaque, surface->geometry.x,
	                          surface->geometry.y);
	pixman_region32_intersect(&opaque, &opaque, &bbox);
	pixman_region32_union(clipped, clipped, &opaque);

	pixman_region32_fini(&damage);
	pixman_region32_fini(&bbox);
	pixman_region32_fini(&opaque);
}

static void
tw_layer_renderer_stack_damage(struct tw_backend *backend,
                               struct tw_plane *plane)
{
	struct tw_surface *surface;
	struct tw_backend_output *output;
	struct tw_layers_manager *layers = &backend->layers_manager;

	//the clip the total coverred region, opaque is the per-plane covered
	//region. For now we have only one plane
	pixman_region32_t opaque;

	SCOPE_PROFILE_BEG();
	pixman_region32_init(&opaque);
	wl_list_for_each(surface, &layers->views,
	                 links[TW_VIEW_GLOBAL_LINK]) {

		surface_accumulate_damage(surface, &opaque);
	}

	pixman_region32_fini(&opaque);

	wl_list_for_each(output, &backend->heads, link) {
		pixman_region32_t output_damage;

		pixman_region32_init(&output_damage);
		pixman_region32_intersect_rect(&output_damage, &plane->damage,
		                               output->state.x,
		                               output->state.y,
		                               output->state.w,
		                               output->state.h);
		pixman_region32_subtract(&plane->damage, &plane->damage,
		                         &output_damage);
		pixman_region32_translate(&output_damage,
		                          -output->state.x,
		                          -output->state.y);
		pixman_region32_copy(output->state.pending_damage,
		                     &output_damage);
		pixman_region32_fini(&output_damage);
	}

	SCOPE_PROFILE_END();
}

/******************************************************************************
 * actual painting
 * even libweston is doing better than this.
 *****************************************************************************/

static void
layer_renderer_scissor_surface(struct tw_renderer *renderer,
                               struct tw_backend_output *output,
                               pixman_box32_t *box)
{
	//the box are in global space, we would need to convert them into
	//output_space
	pixman_box32_t scr_box;

	if (box != NULL) {
		glEnable(GL_SCISSOR_TEST);
		//since the surface is y-down
		tw_mat3_box_transform(&output->state.view_2d,
		                      &scr_box, box);

		glScissor(scr_box.x1, scr_box.y1,
		          scr_box.x2-scr_box.x1, scr_box.y2-scr_box.y1);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
}

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
	//TODO: this cleanup function will be replaced when we render only the
	//damage part

	//restore the scissor information for cleaning up the canvas
	glDisable(GL_SCISSOR_TEST);
	//TODO: the viewport is clearly not correct, since the output will have
	//scale difference, by then we will need to update the viewport, damage
	//and project matrix
	glViewport(0, 0, output->state.w, output->state.h);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	renderer->viewport_h = output->state.h;
	renderer->viewport_w = output->state.w;

	//now we cannot use clear buffer to clean up the damages anymore
#if defined (_TW_DEBUG_DAMAGE) || defined (_TW_DEBUG_CLIP)
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
#endif
}

#if defined(_TW_DEBUG_CLIP)

static void
layer_render_paint_surface_clip(struct tw_surface *surface,
                                struct tw_layer_renderer *rdr,
                                struct tw_backend_output *o,
                                const struct tw_mat3 *proj)
{
	int nrects;
	pixman_box32_t *boxes;
	//purple color for clip
	GLfloat debug_colors[4] = {1.0, 0.0, 1.0, 1.0};
	struct tw_quad_color_shader *shader =
		&rdr->color_quad_shader;

	glUseProgram(shader->prog);
	glUniformMatrix3fv(shader->uniform.proj, 1, GL_FALSE, proj->d);
	glUniform4f(shader->uniform.color, debug_colors[0], debug_colors[1],
	            debug_colors[2], debug_colors[3]);
	glUniform1f(shader->uniform.alpha, 0.5);

	boxes = pixman_region32_rectangles(&surface->clip, &nrects);
	for (int i = 0; i < nrects; i++) {
		layer_renderer_scissor_surface(&rdr->base, o, &boxes[i]);
		layer_renderer_draw_quad(false);
	}
}

#endif


static void
layer_renderer_paint_surface(struct tw_surface *surface,
                             struct tw_layer_renderer *rdr,
                             struct tw_backend_output *o,
                             pixman_region32_t *output_damage)
{
	int nrects;
	pixman_box32_t *boxes;
	struct tw_mat3 proj, tmp;
	struct tw_quad_tex_shader *shader;
	struct tw_render_texture *texture = surface->buffer.handle.ptr;
	pixman_region32_t damage;

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
	//scope start
	SCOPE_PROFILE_BEG();

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

	//extracting damages
	pixman_region32_init(&damage);
	pixman_region32_intersect(&damage, &surface->clip, output_damage);

#if defined (_TW_DEBUG_CLIP)
	boxes = pixman_region32_rectangles(&surface->clip, &nrects);
#else
	boxes = pixman_region32_rectangles(&damage, &nrects);
#endif

	for (int i = 0; i < nrects; i++) {
		layer_renderer_scissor_surface(&rdr->base, o, &boxes[i]);
		layer_renderer_draw_quad(texture->inverted_y);
	}

	pixman_region32_fini(&damage);

#if defined(_TW_DEBUG_CLIP)
	layer_render_paint_surface_clip(surface, rdr, o, &proj);
#endif
	SCOPE_PROFILE_END();
}

static inline void
layer_renderer_compose_output_buffer_damage(struct tw_backend_output *output,
                                            pixman_region32_t *damage,
                                            int buffer_age)
{
	pixman_region32_t *damages[2] = {
		output->state.curr_damage,
		output->state.prev_damage,
	};

	pixman_region32_copy(damage, output->state.pending_damage);
	for (int i = 0; i < buffer_age; i++)
		pixman_region32_union(damage, damage, damages[i]);
}

static void
layer_renderer_repaint_output(struct tw_renderer *renderer,
                              struct tw_backend_output *output,
                              int buffer_age)
{
	struct tw_surface *surface;
	struct tw_presentation_feedback *feedback, *tmp;
	struct tw_backend *backend = output->backend;
	struct tw_layers_manager *manager = &backend->layers_manager;
	struct tw_layer_renderer *layer_render =
		container_of(renderer, struct tw_layer_renderer, base);
	struct timespec now;
	pixman_region32_t output_damage;
	uint32_t now_int;

	SCOPE_PROFILE_BEG();

	tw_backend_build_surface_list(backend);
	pixman_region32_init(&output_damage);

	//move to plane
	wl_list_for_each(surface, &manager->views,
	                 links[TW_VIEW_GLOBAL_LINK]) {
		surface->current->plane = &layer_render->main_plane;
	}
	tw_layer_renderer_stack_damage(backend, &layer_render->main_plane);
	layer_renderer_compose_output_buffer_damage(output, &output_damage,
	                                            buffer_age);

	layer_renderer_cleanup_buffer(renderer, output);

	//for non-opaque surface to work, you really have to draw in reverse
	//order
	wl_list_for_each_reverse(surface, &manager->views,
	                         links[TW_VIEW_GLOBAL_LINK]) {

		layer_renderer_paint_surface(surface, layer_render, output,
		                             &output_damage);

		clock_gettime(CLOCK_MONOTONIC, &now);
		now_int = now.tv_sec * 1000 + now.tv_nsec / 1000000;
		tw_surface_flush_frame(surface, now_int);
	}
	//presentation feebacks
	clock_gettime(CLOCK_MONOTONIC, &now);
	wl_list_for_each_safe(feedback, tmp, &backend->presentation.feedbacks,
	                      link) {
		struct wl_resource *wl_output =
			tw_backend_output_get_wl_output(
				output, feedback->surface->resource);
		tw_presentation_feeback_sync(feedback, wl_output, &now);
	}
	pixman_region32_fini(&output_damage);

	SCOPE_PROFILE_END();
}

static void
layer_renderer_handle_surface_destroy(struct tw_renderer *renderer,
                                      struct tw_surface *surface)
{
	struct tw_layer_renderer *layer_render =
		container_of(renderer, struct tw_layer_renderer, base);
	pixman_region32_union(&layer_render->main_plane.damage,
	                      &layer_render->main_plane.damage,
	                      &surface->clip);
}

static void
tw_layer_renderer_destroy(struct wlr_renderer *wlr_renderer)
{
	struct tw_layer_renderer *renderer =
		container_of(wlr_renderer, struct tw_layer_renderer,
		             base.base);

	tw_quad_color_shader_fini(&renderer->color_quad_shader);
	tw_quad_tex_blend_shader_fini(&renderer->quad_shader);
	tw_quad_tex_ext_blend_shader_fini(&renderer->quad_shader);
	tw_renderer_base_fini(&renderer->base);

	tw_plane_fini(&renderer->main_plane);

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
	tw_plane_init(&renderer->main_plane);

	tw_quad_color_shader_init(&renderer->color_quad_shader);
	tw_quad_tex_blend_shader_init(&renderer->quad_shader);
	tw_quad_tex_ext_blend_shader_init(&renderer->ext_quad_shader);
	renderer->base.wlr_impl.destroy = tw_layer_renderer_destroy;
	wlr_renderer_init(&renderer->base.base, &renderer->base.wlr_impl);

	renderer->base.repaint_output = layer_renderer_repaint_output;
	renderer->base.notify_surface_destroy =
		layer_renderer_handle_surface_destroy;

	return &renderer->base.base;
}
