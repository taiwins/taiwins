/*
 * egl_simple_pipeline.c - an layer renderer implementation for egl context
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
#include <stdlib.h>
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
#include <taiwins/output_device.h>
#include <taiwins/profiling.h>
#include <taiwins/render_context_egl.h>
#include <taiwins/render_surface.h>
#include <taiwins/render_pipeline.h>

struct tw_egl_layer_render_pipeline {
	struct tw_render_pipeline base;
	//TODO: this is still a temporary solution,
	struct tw_plane main_plane;

	struct tw_egl_quad_shader quad_shader;
	/* for debug rendering */
	struct tw_egl_quad_shader color_quad_shader;
	/* for external sampler */
	struct tw_egl_quad_shader ext_quad_shader;

	struct tw_layers_manager *manager;
};

/******************************************************************************
 * damage stacking
 *****************************************************************************/

static void
surface_accumulate_damage(struct tw_surface *surface,
                          pixman_region32_t *clipped)
{
	pixman_region32_t damage, bbox, opaque;
	struct tw_view *current = surface->current;
	struct tw_render_surface *render_surface =
		wl_container_of(surface, render_surface, surface);

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
		pixman_region32_intersect_rect(&damage,
		                               &current->surface_damage,
		                               0, 0,
		                               surface->geometry.xywh.width,
		                               surface->geometry.xywh.height);
		pixman_region32_translate(&damage, surface->geometry.xywh.x,
		                          surface->geometry.xywh.y);
		pixman_region32_intersect(&damage, &damage, &bbox);
	}
	pixman_region32_subtract(&damage, &damage, clipped);
	pixman_region32_union(&current->plane->damage,
	                      &current->plane->damage, &damage);
	//update the clip region here. but yeah, our surface region is not
	//correct at all.
	pixman_region32_subtract(&render_surface->clip, &bbox, clipped);
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
pipeline_stack_damage(struct tw_egl_layer_render_pipeline *pipeline,
                      struct tw_plane *plane)
{
	struct tw_surface *surface;
	struct tw_render_output *output;
	struct tw_render_context *ctx = pipeline->base.ctx;
	struct tw_layers_manager *layers = pipeline->manager;

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

	wl_list_for_each(output, &ctx->outputs, link) {
		pixman_region32_t output_damage;
		pixman_rectangle32_t rect =
			tw_output_device_geometry(&output->device);

		pixman_region32_init(&output_damage);
		pixman_region32_intersect_rect(&output_damage, &plane->damage,
		                               rect.x, rect.y,
		                               rect.width, rect.height);
		pixman_region32_subtract(&plane->damage, &plane->damage,
		                         &output_damage);
		pixman_region32_translate(&output_damage, -rect.x, -rect.y);
		pixman_region32_copy(output->state.pending_damage,
		                     &output_damage);
		pixman_region32_fini(&output_damage);
	}

	SCOPE_PROFILE_END();
}

static inline void
pipeline_compose_output_buffer_damage(struct tw_render_output *output,
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

/******************************************************************************
 * repaints
 *****************************************************************************/

static void
pipeline_scissor_surface(struct tw_render_output *output,
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
pipeline_draw_quad(bool y_inverted)
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
pipeline_cleanup_buffer(struct tw_render_output *output)
{
	//TODO: this cleanup function will be replaced when we render only the
	//damage part
	unsigned int width, height;

	tw_output_device_raw_resolution(&output->device, &width, &height);

	//restore the scissor information for cleaning up the canvas
	glDisable(GL_SCISSOR_TEST);
	//TODO: the viewport is clearly not correct, since the output will have
	//scale difference, by then we will need to update the viewport, damage
	//and project matrix
	glViewport(0, 0, width, height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	//now we cannot use clear buffer to clean up the damages anymore
#if defined( _TW_DEBUG_DAMAGE ) || defined( _TW_DEBUG_CLIP )
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
#endif
}

#if defined ( _TW_DEBUG_CLIP )

static void
pipeline_paint_surface_clip(struct tw_surface *surface,
                            struct tw_egl_layer_render_pipeline *pipeline,
                            struct tw_render_output *o,
                            const struct tw_mat3 *proj)
{
	int nrects;
	pixman_box32_t *boxes;
	//purple color for clip
	GLfloat debug_colors[4] = {1.0, 0.0, 1.0, 1.0};
	struct tw_egl_quad_shader *shader = &pipeline->color_quad_shader;

	glUseProgram(shader->prog);
	glUniformMatrix3fv(shader->uniform.proj, 1, GL_FALSE, proj->d);
	glUniform4f(shader->uniform.target, debug_colors[0], debug_colors[1],
	            debug_colors[2], debug_colors[3]);
	glUniform1f(shader->uniform.alpha, 0.5);

	boxes = pixman_region32_rectangles(&surface->clip, &nrects);
	for (int i = 0; i < nrects; i++) {
		pipeline_scissor_surface(o, &boxes[i]);
		pipeline_draw_quad(false);
	}
}

#endif

static void
pipeline_paint_surface(struct tw_surface *surface,
                       struct tw_egl_layer_render_pipeline *pipeline,
                       struct tw_render_output *o,
                       pixman_region32_t *output_damage)
{
	int nrects;
	pixman_box32_t *boxes;
	struct tw_mat3 proj, tmp;
	struct tw_egl_quad_shader *shader;
	struct tw_egl_render_texture *texture =
		wl_container_of(surface->buffer.handle.ptr, texture, base);
	struct tw_render_surface *render_surface =
		wl_container_of(surface, render_surface, surface);
	pixman_region32_t damage;
	unsigned int w, h;

	if (!texture)
		return;

	switch (texture->target) {
	case GL_TEXTURE_2D:
		shader = &pipeline->quad_shader;
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		shader = &pipeline->ext_quad_shader;
		break;
	default:
		tw_logl_level(TW_LOG_ERRO, "unknown texture format!");
		return;
	}
	//scope start
	SCOPE_PROFILE_BEG();

	tw_output_device_raw_resolution(&o->device, &w, &h);

	tw_mat3_multiply(&tmp,
	                 &o->state.view_2d,
	                 &surface->geometry.transform);
	tw_mat3_ortho_proj(&proj, w, h);
	tw_mat3_multiply(&proj, &proj, &tmp);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->gltex);
	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUseProgram(shader->prog);
	glUniformMatrix3fv(shader->uniform.proj, 1, GL_FALSE, proj.d);
	glUniform1i(shader->uniform.target, 0);
	glUniform1f(shader->uniform.alpha, 1.0f);

	//extracting damages
	pixman_region32_init(&damage);
	pixman_region32_intersect(&damage, &render_surface->clip,
	                          output_damage);

#if defined( _TW_DEBUG_CLIP )
	boxes = pixman_region32_rectangles(&surface->clip, &nrects);
#else
	boxes = pixman_region32_rectangles(&damage, &nrects);
#endif

	for (int i = 0; i < nrects; i++) {
		pipeline_scissor_surface(o, &boxes[i]);
		pipeline_draw_quad(texture->base.inverted_y);
	}

	pixman_region32_fini(&damage);

#if defined ( _TW_DEBUG_CLIP )
	layer_render_paint_surface_clip(surface, rdr, o, &proj);
#endif
	SCOPE_PROFILE_END();
}

/******************************************************************************
 * pipeline implementation
 *****************************************************************************/

static void
pipeline_repaint_output(struct tw_render_pipeline *base,
                        struct tw_render_output *output, int buffer_age)
{
	struct tw_surface *surface;
	struct tw_egl_layer_render_pipeline *pipeline =
		wl_container_of(base, pipeline, base);
        struct tw_layers_manager *manager = pipeline->manager;
        struct timespec now;
	pixman_region32_t output_damage;
	uint32_t now_int;

	SCOPE_PROFILE_BEG();

	tw_render_context_build_view_list(base->ctx, pipeline->manager);
	pixman_region32_init(&output_damage);

	//move to plane
	wl_list_for_each(surface, &manager->views,
	                 links[TW_VIEW_GLOBAL_LINK]) {
		surface->current->plane = &pipeline->main_plane;
	}


	pipeline_stack_damage(pipeline, &pipeline->main_plane);
	pipeline_compose_output_buffer_damage(output, &output_damage,
	                                      buffer_age);

	pipeline_cleanup_buffer(output);

	//for non-opaque surface to work, you really have to draw in reverse
	//order
	wl_list_for_each_reverse(surface, &manager->views,
	                         links[TW_VIEW_GLOBAL_LINK]) {

		pipeline_paint_surface(surface, pipeline, output,
		                       &output_damage);

		clock_gettime(CLOCK_MONOTONIC, &now);
		now_int = now.tv_sec * 1000 + now.tv_nsec / 1000000;
		tw_surface_flush_frame(surface, now_int);
	}
	//presentation feebacks

	pixman_region32_fini(&output_damage);

	SCOPE_PROFILE_END();
}

static void
pipeline_destroy(struct tw_render_pipeline *base)
{
	struct tw_egl_layer_render_pipeline *pipeline =
		wl_container_of(base, pipeline, base);

	tw_plane_fini(&pipeline->main_plane);
	tw_render_pipeline_fini(base);

	tw_egl_quad_color_shader_fini(&pipeline->color_quad_shader);
	tw_egl_quad_tex_shader_fini(&pipeline->quad_shader);
	tw_egl_quad_texext_shader_fini(&pipeline->ext_quad_shader);
        free(pipeline);
}

struct tw_render_pipeline *
tw_egl_render_pipeline_create_default(struct tw_render_context *ctx,
                                      struct tw_layers_manager *manager)
{
	struct tw_egl_layer_render_pipeline *pipeline =
		calloc(1, sizeof(*pipeline));

        pipeline->manager = manager;
        tw_render_pipeline_init(&pipeline->base, "EGL Sample", ctx);

	tw_egl_quad_color_shader_init(&pipeline->color_quad_shader);
	tw_egl_quad_tex_shader_init(&pipeline->quad_shader);
	tw_egl_quad_texext_shader_init(&pipeline->ext_quad_shader);
	tw_plane_init(&pipeline->main_plane);
	pipeline->base.impl.destroy = pipeline_destroy;
	pipeline->base.impl.repaint_output = pipeline_repaint_output;

	return &pipeline->base;
}
