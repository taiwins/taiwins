#define NK_IMPLEMENTATION
#include <time.h>
#include <assert.h>

#ifdef _WITH_NVIDIA
#include <eglexternalplatform.h>
#endif

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>


#include <GL/gl.h>
#include <GL/glext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#include "egl.h"
#include "shellui.h"
#include "client.h"
#include <wayland-taiwins-shell-client-protocol.h>

/*
 * ===============================================================
 *
 *                 EGL application book-keeping
 *
 * ===============================================================
 */

static void
widget_key_cb(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier);

static void
widget_cursor_motion_cb(struct app_surface *surf, uint32_t sx, uint32_t sy);
static void
widget_cursor_button_cb(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy);

static void
widget_cursor_axis_cb(struct app_surface *surf, int speed, int direction, uint32_t sx, uint32_t sy);

void
panel_setup_widget_input(struct shell_panel *panel)
{
	struct app_surface *widget_surf = &panel->widget_surface;
	appsurface_init_input(widget_surf,
			      widget_key_cb,
			      widget_cursor_motion_cb,
			      widget_cursor_button_cb,
			      widget_cursor_axis_cb);
}

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifndef NK_EGLAPP_DOUBLE_CLICK_LO
#define NK_EGLAPP_DOUBLE_CLICK_LO 0.02
#endif

#ifndef NK_EGLAPP_DOUBLE_CLICK_HI
#define NK_EGLAPP_DOUBLE_CLICK_HI 0.2
#endif

//have a small limited vertex buffer is certainly great
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

void
widget_key_cb(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier)
{
	struct shell_panel *p = container_of(surf, struct shell_panel, widget_surface);
	struct nk_context *ctx = &p->ctx;
	//maybe you will need to call the render here as well
//	nk_input_begin(ctx);
	//now we need a key translation library...
	nk_input_key(ctx, NK_KEY_LEFT, true);
//	nk_input_end(ctx);
}

void
widget_cursor_motion_cb(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct shell_panel *p = container_of(surf, struct shell_panel, widget_surface);
	struct nk_context *ctx = &p->ctx;
//	nk_input_begin(ctx);
	//now we need a key translation library...
	nk_input_motion(ctx, sx, sy);
//	nk_input_end(ctx);
}

void
widget_cursor_button_cb(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy)
{
	struct shell_panel *p = container_of(surf, struct shell_panel, widget_surface);
	struct nk_context *ctx = &p->ctx;
	nk_input_begin(ctx);
	nk_input_button(ctx, (btn) ? NK_BUTTON_LEFT : NK_BUTTON_RIGHT, sx, sy, true);
	nk_input_end(ctx);
}

void
widget_cursor_axis_cb(struct app_surface *surf, int speed, int direction, uint32_t sx, uint32_t sy)
{
	struct shell_panel *p = container_of(surf, struct shell_panel, widget_surface);
	struct nk_context *ctx = &p->ctx;
	nk_input_begin(ctx);
	nk_input_scroll(ctx, nk_vec2(speed * direction, speed *(1-direction)));
	nk_input_end(ctx);
}

//I can actually just samperate a
NK_API void
nk_egl_char_callback(struct shell_widget *win, unsigned int codepoint);
NK_API void
nk_egl_render(struct shell_panel *app, struct shell_widget *w, enum nk_anti_aliasing AA,
	      int max_vertex_buffer, int max_element_buffer);

NK_API void nk_egl_font_stash_begin(struct shell_panel *app, struct nk_font_atlas **atlas);
NK_API void nk_egl_font_stash_end(struct shell_panel *app);


void
shell_panel_init_nklear(struct shell_panel *panel)
{
	struct nk_font_atlas *atlas;
	//I am not sure if I need to init this first
//	nk_buffer_init_default(&panel->cmds);
	//change that to fixed later
	nk_init_default(&panel->ctx, 0);
	nk_egl_font_stash_begin(panel, &atlas);
	nk_egl_font_stash_end(panel);
}

void
shell_widget_destroy(struct shell_widget *app)
{
	cairo_destroy(app->icon.ctxt);
	cairo_surface_destroy(app->icon.isurf);
}


NK_INTERN void
nk_egl_upload_atlas(struct shell_panel *app, const void *image, int width, int height)
{
	glGenTextures(1, &app->font_tex);
	glBindTexture(GL_TEXTURE_2D, app->font_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height,
		     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
}

NK_API void
nk_egl_font_stash_begin(struct shell_panel *app, struct nk_font_atlas **atlas)
{
    nk_font_atlas_init_default(&app->atlas);
    nk_font_atlas_begin(&app->atlas);
    *atlas = &app->atlas;
}

NK_API void
nk_egl_font_stash_end(struct shell_panel *app)
{
    const void *image; int w, h;
    image = nk_font_atlas_bake(&app->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_egl_upload_atlas(app, image, w, h);
    nk_font_atlas_end(&app->atlas, nk_handle_id((int)app->font_tex), &app->null);
    if (app->atlas.default_font) {
	    //we should have here though
	nk_style_set_font(&app->ctx, &app->atlas.default_font->handle);
    }
}


static void
_test_draw_triangle(struct shell_panel *app)
{
	const struct egl_nk_vertex triangle[3] = {
		{
			.position = {0.5, 0.25},
			.uv = {0.0, 0.0},
			.col = {1, 0, 0, 1},
		},
		{
			.position = {0.25, 0.75},
			.uv = {0.0, 0.0},
			.col = {0, 1, 0, 1},
		},
		{
			.position = {0.75, 0.75},
			.uv = {0.0, 0.0},
			.col = {0, 0, 1, 1},
		},
	};
	glBindVertexArray(app->vao);
	glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(struct egl_nk_vertex) * 3, &triangle[0], GL_STATIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

NK_API void
nk_egl_render(struct shell_panel *app, struct shell_widget *widget,
	      enum nk_anti_aliasing AA, int max_vertex_buffer,
	      int max_element_buffer)
{
	struct nk_buffer vbuf, ebuf;
	//it is column major
	GLfloat ortho[4][4] = {
		{ 2.0f,  0.0f,  0.0f, 0.0f},
		{ 0.0f, -2.0f,  0.0f, 0.0f},
		{ 0.0f,  0.0f, -1.0f, 0.0f},
		{-1.0f,  1.0f,  0.0f, 1.0f},
	};
	ortho[0][0] /= (GLfloat)widget->width;
	ortho[1][1] /= (GLfloat)widget->height;

	//setup the global state
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glActiveTexture(GL_TEXTURE0);

	glUseProgram(app->glprog);
	glUniform1i(app->uniform_tex, 0);
	glUniformMatrix4fv(app->uniform_proj, 1, GL_FALSE, &ortho[0][0]);
	//we can successfully draw triangles now
	{
		//convert the command queue
		const struct nk_draw_command *cmd;
		void *vertices = NULL;
		void *elements = NULL;
		const nk_draw_index *offset = NULL;

		glBindVertexArray(app->vao);
		glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ebo);
		//in this case, we probably just need to use glBufferData once
		//and the rest depends on ...
		glBufferData(GL_ARRAY_BUFFER, max_vertex_buffer,
			     NULL, GL_STREAM_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, max_element_buffer,
			     NULL, GL_STREAM_DRAW);
		vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		elements = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
		{
		//convert
		struct nk_convert_config config;
		static const struct nk_draw_vertex_layout_element vertex_layout[] = {
			{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
			 NK_OFFSETOF(struct egl_nk_vertex, position)},
			{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
			 NK_OFFSETOF(struct egl_nk_vertex, uv)},
			{NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT,
			 NK_OFFSETOF(struct egl_nk_vertex, col)},
			{NK_VERTEX_LAYOUT_END}
		};
		nk_memset(&config, 0, sizeof(config));
		config.vertex_layout = vertex_layout;
		config.vertex_size = sizeof(struct egl_nk_vertex);
		config.vertex_alignment = NK_ALIGNOF(struct egl_nk_vertex);
		config.null = app->null;
		config.circle_segment_count = 2;;
		config.curve_segment_count = 22;
		config.arc_segment_count = 2;;
		config.global_alpha = 1.0f;
		config.shape_AA = AA;
		config.line_AA = AA;

		nk_buffer_init_fixed(&vbuf, vertices, (size_t)max_vertex_buffer);
		nk_buffer_init_fixed(&ebuf, elements, (size_t)max_element_buffer);
		nk_convert(&app->ctx, &app->cmds, &vbuf, &ebuf, &config);
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

		nk_draw_foreach(cmd, &app->ctx, &app->cmds) {
			if (!cmd->elem_count)
				continue;
			glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
			GLint scissor_region[4] = {
				(GLint)(cmd->clip_rect.x * app->fb_scale.x),
				(GLint)((widget->height -
					 (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) *
					app->fb_scale.y),
				(GLint)(cmd->clip_rect.w * app->fb_scale.x),
				(GLint)(cmd->clip_rect.h * app->fb_scale.y),
			};
			glScissor(scissor_region[0], scissor_region[1],
				  scissor_region[2], scissor_region[3]);
			glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count,
				       GL_UNSIGNED_SHORT, offset);
			offset += cmd->elem_count;
		}
		nk_clear(&app->ctx);
	}
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
}


NK_API void
nk_egl_char_callback(struct shell_widget *win, unsigned int codepoint)
{
    if (win->text_len < NK_EGLAPP_TEXT_MAX)
	win->text[win->text_len++] = codepoint;
}

//you don't even need to be here
void
nk_egl_new_frame(struct shell_panel *app)
{
	struct nk_context *ctx = &app->ctx;
	struct shell_widget *widget = app->active_one;

	nk_input_begin(ctx);
	for (int i = 0; i < widget->text_len; i++)
		nk_input_unicode(ctx, widget->text[i]);
	nk_input_end(ctx);
	nk_buffer_init_default(&app->cmds);
	//this should be a draw command
	if (nk_begin(ctx, "eglapp", nk_rect(0,0, widget->width, widget->height),
		     NK_WINDOW_BORDER)) {
		//TODO, change the draw function to app->draw_widget(app);
		enum {EASY, HARD};
		static int op = EASY;
		static int property = 20;
		nk_layout_row_static(ctx, 30, 80, 2);
		nk_button_label(ctx, "button");
		nk_label(ctx, "another", NK_TEXT_LEFT);
		//I can try to use the other textures
		/* if (nk_button_label(ctx, "button")) { */
		/*	fprintf(stderr, "button pressed\n"); */
		/* } */
		nk_layout_row_dynamic(ctx, 30, 2);
		if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
		if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;

		/* nk_layout_row_dynamic(ctx, 25, 1); */
		/* nk_property_int(ctx, "Compression:", 0, &property, 100, 10, 1); */
		/* nk_label(ctx, "background:", NK_TEXT_LEFT); */
		/* nk_layout_row_dynamic(ctx, 25, 1); */
	    }
	nk_end(ctx);
	glViewport(0, 0, widget->width, widget->height);
	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	//free the commands here.
	nk_buffer_free(&app->cmds);
	//temp code
	nk_egl_render(app, widget, NK_ANTI_ALIASING_ON,
		      MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
	eglSwapBuffers(app->eglenv->egl_display, app->eglsurface);
}

struct point2d
widget_find_anchor(struct shell_widget *app)
{
	struct point2d point;
	int x = app->icon.box.x + app->icon.box.w/2;
//	int y = app->icon.box.y;
	int ww = app->width;
//	int wh = app->height;

	struct shell_panel *panel = app->panel;
	unsigned int pw = panel->panelsurf.w;
//	unsigned int ph = panel->panelsurf.h;

	if (x + ww/2 >= pw) {
		point.x = pw - ww;
	} else if (x - ww/2 < 0) {
		point.x = 0;
	} else
		point.x = x - ww/2;
	point.y = 10;
	return point;
}

void
shell_widget_launch(struct shell_widget *app)
{
	struct shell_panel *panel = app->panel;
	panel->active_one = app;
	//figure out where to launch the widget
	struct point2d p = widget_find_anchor(app);
	shell_panel_show_widget(panel, p.x, p.y);
	wl_egl_window_resize(panel->eglwin, app->width, app->height, 0, 0);
	app->panel->fb_scale = nk_vec2(1, 1);
	nk_egl_new_frame(panel);
}
