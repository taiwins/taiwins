#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/input.h>
#include <time.h>
#include <stdbool.h>
#include <wayland-egl.h>
#include <wayland-client.h>
#include <cairo/cairo.h>



#define NK_IMPLEMENTATION
#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

#define MAX_CMD_SIZE = 64 * 1024

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY

#include <cairo/cairo.h>
#include "../client.h"
#include "../ui.h"
#include "nk_wl_internal.h"

struct nk_cairo_backend {
	struct nk_wl_backend base;
	nk_max_cmd_t last_cmds[2];
	struct nk_user_font user_font;
};


typedef void (*nk_cairo_op) (cairo_t *cr, const struct nk_command *cmd);

#ifndef NK_COLOR_TO_FLOAT
#define NK_COLOR_TO_FLOAT(x) ({ (double)x / 255.0; })
#endif

#ifndef NK_CAIRO_DEG_TO_RAD
#define NK_CAIRO_DEG_TO_RAD(x) ({ (double) x * NK_PI / 180.0;})
#endif

static inline void
nk_cairo_set_painter(cairo_t *cr, const struct nk_color *color, unsigned short line_width)
{
	cairo_set_source_rgba(
		cr,
		NK_COLOR_TO_FLOAT(color->r),
		NK_COLOR_TO_FLOAT(color->g),
		NK_COLOR_TO_FLOAT(color->b),
		NK_COLOR_TO_FLOAT(color->a));
	if (line_width != 0)
		cairo_set_line_width(cr, line_width);
}

static void
nk_cairo_noop(cairo_t *cr, const struct nk_command *cmd)
{
	fprintf(stderr, "cairo: no operation applied\n");
}

static void
nk_cairo_scissor(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_scissor *s =
		(const struct nk_command_scissor *) cmd;
	cairo_reset_clip(cr);
	if (s->x >= 0) {
		cairo_rectangle(cr, s->x - 1, s->y - 1,
				s->w+2, s->h+2);
		cairo_clip(cr);
	}
}

static void
nk_cairo_line(cairo_t *cr, const struct nk_command *cmd)
{
       const struct nk_command_line *l =
	       (const struct nk_command_line *) cmd;
       cairo_set_source_rgba(
	       cr,
	       NK_COLOR_TO_FLOAT(l->color.r),
	       NK_COLOR_TO_FLOAT(l->color.g),
	       NK_COLOR_TO_FLOAT(l->color.b),
	       NK_COLOR_TO_FLOAT(l->color.a));
       cairo_set_line_width(cr, l->line_thickness);
       cairo_move_to(cr, l->begin.x, l->begin.y);
       cairo_line_to(cr, l->end.x, l->end.y);
       cairo_stroke(cr);
}

static void
nk_cairo_curve(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_curve *q =
		(const struct nk_command_curve *)cmd;
	nk_cairo_set_painter(cr, &q->color, q->line_thickness);
	cairo_move_to(cr, q->begin.x, q->begin.y);
	cairo_curve_to(cr, q->ctrl[0].x, q->ctrl[0].y,
		       q->ctrl[1].x, q->ctrl[1].y,
		       q->end.x, q->end.y);
	cairo_stroke(cr);
}

static void
nk_cairo_rect(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_rect *r =
		(const struct nk_command_rect *) cmd;
	nk_cairo_set_painter(cr, &r->color, r->line_thickness);
	if (r->rounding == 0)
		cairo_rectangle(cr, r->x, r->y, r->w, r->h);
	else {
		int xl = r->x + r->w - r->rounding;
		int xr = r->x + r->rounding;
		int yl = r->y + r->h - r->rounding;
		int yr = r->y + r->rounding;
		cairo_new_sub_path(cr);
		cairo_arc(cr, xl, yr, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(-90),
			  NK_CAIRO_DEG_TO_RAD(0));
		cairo_arc(cr, xl, yl, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(0),
			  NK_CAIRO_DEG_TO_RAD(90));
		cairo_arc(cr, xr, yl, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(90),
			  NK_CAIRO_DEG_TO_RAD(180));
		cairo_arc(cr, xr, yr, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(180),
			  NK_CAIRO_DEG_TO_RAD(270));
		cairo_close_path(cr);
	}
	cairo_stroke(cr);
}

static void
nk_cairo_rect_filled(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_rect_filled *r =
		(const struct nk_command_rect_filled *)cmd;
	nk_cairo_set_painter(cr, &r->color, 0);
	if (r->rounding == 0)
		cairo_rectangle(cr, r->x, r->y, r->w, r->h);
	else {
		int xl = r->x + r->w - r->rounding;
		int xr = r->x + r->rounding;
		int yl = r->y + r->h - r->rounding;
		int yr = r->y + r->rounding;
		cairo_new_sub_path(cr);
		cairo_arc(cr, xl, yr, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(-90),
			  NK_CAIRO_DEG_TO_RAD(0));
		cairo_arc(cr, xl, yl, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(0),
			  NK_CAIRO_DEG_TO_RAD(90));
		cairo_arc(cr, xr, yl, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(90),
			  NK_CAIRO_DEG_TO_RAD(180));
		cairo_arc(cr, xr, yr, r->rounding,
			  NK_CAIRO_DEG_TO_RAD(180),
			  NK_CAIRO_DEG_TO_RAD(270));
		cairo_close_path(cr);
	}
	cairo_fill(cr);
}

static void
nk_cairo_rect_multi_color(cairo_t *cr, const struct nk_command *cmd)
{
	/* const struct nk_command_rect_multi_color *r =
	   (const struct nk_command_rect_multi_color *) cmd; */
	//TODO?
}

static void
nk_cairo_circle(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_circle *c =
		(const struct nk_command_circle *) cmd;
	nk_cairo_set_painter(cr, &c->color, c->line_thickness);
	//based on the doc from cairo, the save here is to avoid the artifacts
	//of non-uniform width size of curve
	cairo_save(cr);
	cairo_translate(cr, c->x+ c->w / 2.0,
		c->y + c->h / 2.0);
	//apply the scaling in a new path
	cairo_new_sub_path(cr);
	cairo_scale(cr, c->w/2.0, c->h/2.0);
	cairo_arc(cr, 0, 0, 1, NK_CAIRO_DEG_TO_RAD(0),
		  NK_CAIRO_DEG_TO_RAD(360));
	cairo_close_path(cr);
	//now we restore the matrix
	cairo_restore(cr);
	cairo_stroke(cr);
}

static void
nk_cairo_circle_filled(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_circle_filled *c =
		(const struct nk_command_circle_filled *)cmd;
	nk_cairo_set_painter(cr, &c->color, 0);
	cairo_save(cr);
	cairo_translate(cr, c->x+c->w/2.0, c->y+c->h/2.0);
	cairo_scale(cr, c->w/2.0, c->h/2.0);
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0, 0, 1, NK_CAIRO_DEG_TO_RAD(0),
			   NK_CAIRO_DEG_TO_RAD(360));
	cairo_close_path(cr);
	cairo_restore(cr);
	cairo_fill(cr);
}

static void
nk_cairo_arc(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_arc *a =
		(const struct nk_command_arc *)cmd;
	nk_cairo_set_painter(cr, &a->color, a->line_thickness);
	cairo_arc(cr, a->cx, a->cy, a->r,
		  NK_CAIRO_DEG_TO_RAD(a->a[0]),
		  NK_CAIRO_DEG_TO_RAD(a->a[1]));
	cairo_stroke(cr);
}

static void
nk_cairo_arc_filled(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_arc_filled *a =
		(const struct nk_command_arc_filled *) cmd;
	nk_cairo_set_painter(cr, &a->color, 0);
	cairo_arc(cr, a->cx, a->cy, a->r,
		  NK_CAIRO_DEG_TO_RAD(a->a[0]),
		  NK_CAIRO_DEG_TO_RAD(a->a[1]));
	cairo_fill(cr);
}

static void
nk_cairo_triangle(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_triangle *t =
		(const struct nk_command_triangle *)cmd;
	nk_cairo_set_painter(cr, &t->color, t->line_thickness);
	cairo_move_to(cr, t->a.x, t->a.y);
	cairo_line_to(cr, t->b.x, t->b.y);
	cairo_line_to(cr, t->c.x, t->c.y);
	cairo_close_path(cr);
	cairo_stroke(cr);
}

static void
nk_cairo_triangle_filled(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_triangle_filled *t =
		(const struct nk_command_triangle_filled *)cmd;
	nk_cairo_set_painter(cr, &t->color, 0);
	cairo_move_to(cr, t->a.x, t->a.y);
	cairo_line_to(cr, t->b.x, t->b.y);
	cairo_line_to(cr, t->c.x, t->c.y);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void
nk_cairo_polygon(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_polygon *p =
		(const struct nk_command_polygon *)cmd;
	nk_cairo_set_painter(cr, &p->color, p->line_thickness);
	cairo_move_to(cr, p->points[0].x, p->points[0].y);
	for (int i = 1; i < p->point_count; i++)
		cairo_line_to(cr, p->points[i].x, p->points[i].y);
	cairo_close_path(cr);
	cairo_stroke(cr);
}

static void
nk_cairo_polygon_filled(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_polygon_filled *p =
		(const struct nk_command_polygon_filled *)cmd;
	nk_cairo_set_painter(cr, &p->color, 0);
	cairo_move_to(cr, p->points[0].x, p->points[0].y);
	for (int i = 1; i < p->point_count; i++)
		cairo_line_to(cr, p->points[i].x, p->points[i].y);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void
nk_cairo_polyline(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_polyline *p =
		(const struct nk_command_polyline *)cmd;
	nk_cairo_set_painter(cr, &p->color, p->line_thickness);
	cairo_move_to(cr, p->points[0].x, p->points[0].y);
	for (int i = 1; i < p->point_count; i++)
		cairo_line_to(cr, p->points[i].x, p->points[i].y);
	cairo_stroke(cr);
}

static void
nk_cairo_text(cairo_t *cr, const struct nk_command *cmd)
{
	const struct nk_command_text *t =
		(const struct nk_command_text *)cmd;
	cairo_glyph_t *glyphs = NULL;
	//TODO finish this
}

static void
nk_cairo_image(cairo_t *cr, const struct nk_command *cmd)
{

}

static void
nk_cairo_custom(cairo_t *cr, const struct nk_command *cmd)
{

}

const nk_cairo_op nk_cairo_ops[] = {
	nk_cairo_noop,
	nk_cairo_scissor,
	nk_cairo_line,
	nk_cairo_curve,
	nk_cairo_rect,
	nk_cairo_rect_filled,
	nk_cairo_rect_multi_color,
	nk_cairo_circle,
	nk_cairo_circle_filled,
	nk_cairo_arc,
	nk_cairo_arc_filled,
	nk_cairo_triangle,
	nk_cairo_triangle_filled,
	nk_cairo_polygon,
	nk_cairo_polygon_filled,
	nk_cairo_polyline,
	nk_cairo_text,
	nk_cairo_image,
	nk_cairo_custom,
};

#ifdef _GNU_SOURCE

#define NO_COMMAND "nk_cairo: command mismatch"
_Static_assert(NK_COMMAND_NOP == 0, NO_COMMAND);
_Static_assert(NK_COMMAND_SCISSOR == 1, NO_COMMAND);
_Static_assert(NK_COMMAND_LINE == 2, NO_COMMAND);
_Static_assert(NK_COMMAND_CURVE == 3, NO_COMMAND);
_Static_assert(NK_COMMAND_RECT == 4, NO_COMMAND);
_Static_assert(NK_COMMAND_RECT_FILLED == 5, NO_COMMAND);
_Static_assert(NK_COMMAND_RECT_MULTI_COLOR == 6, NO_COMMAND);
_Static_assert(NK_COMMAND_CIRCLE == 7, NO_COMMAND);
_Static_assert(NK_COMMAND_CIRCLE_FILLED == 8, NO_COMMAND);
_Static_assert(NK_COMMAND_ARC == 9, NO_COMMAND);
_Static_assert(NK_COMMAND_ARC_FILLED == 10, NO_COMMAND);
_Static_assert(NK_COMMAND_TRIANGLE == 11, NO_COMMAND);
_Static_assert(NK_COMMAND_TRIANGLE_FILLED == 12, NO_COMMAND);
_Static_assert(NK_COMMAND_POLYGON == 13, NO_COMMAND);
_Static_assert(NK_COMMAND_POLYGON_FILLED == 14, NO_COMMAND);
_Static_assert(NK_COMMAND_POLYLINE == 15, NO_COMMAND);
_Static_assert(NK_COMMAND_TEXT == 16, NO_COMMAND);
_Static_assert(NK_COMMAND_IMAGE == 17, NO_COMMAND);
_Static_assert(NK_COMMAND_CUSTOM == 18, NO_COMMAND);

#endif



static void
nk_cairo_render(struct wl_buffer *buffer, struct nk_wl_backend *bkend,
		int32_t w, int32_t h,
		cairo_format_t format)
{
	const struct nk_command *cmd = NULL;
	void *data = shm_pool_buffer_access(buffer);

	cairo_surface_t *csurface =
		cairo_image_surface_create_for_data(
			(unsigned char *)data, format, w, h,
			cairo_format_stride_for_width(format, w));
	cairo_t *cr = cairo_create(csurface);
	//1) clean this buffer using its background color, or maybe nuklear does
	//that already

	cairo_set_source_rgb(cr, bkend->main_color.r, bkend->main_color.g,
			     bkend->main_color.b);
	cairo_paint(cr);

	//it is actually better to implement a table look up than switch command
	nk_foreach(cmd, &bkend->ctx) {
		nk_cairo_ops[cmd->type](cr, cmd);
		/* switch (cmd->type) { */
		/* case NK_COMMAND_NOP: */
		/*	fprintf(stderr, "cairo: no nuklear operation\n"); */
		/*	break; */
		/* case NK_COMMAND_SCISSOR: */
		/*	break; */
		/* } */
	}

	cairo_destroy(cr);
	cairo_surface_destroy(csurface);

}

static void
nk_wl_render(struct nk_wl_backend *bkend)
{
	struct app_surface *surf = bkend->app_surface;
	//selecting the free frame
	struct wl_buffer *free_buffer = NULL;
	int32_t w, h;

	w = surf->w; h = surf->h;
	for (int i = 0; i < 2; i++) {
		if (surf->committed[i] || surf->dirty[i])
			continue;
		free_buffer = surf->wl_buffer[i];
		break;
	}
	if (!free_buffer)
		return;
	//there are two case: 1) it looks exactly like last frame, so we just
	// return 2) it looks exactly like the last frame in this buffer, we can
	// simply swap this buffer
	if (!nk_wl_need_redraw(bkend))
		return;

	//otherwise can start the draw call now
	nk_cairo_render(free_buffer, bkend, w, h,
			translate_wl_shm_format(surf->pool->format));
	wl_surface_attach(surf->wl_surface, free_buffer, 0, 0);
	wl_surface_damage(surf->wl_surface, 0, 0, surf->w, surf->h);
	wl_surface_commit(surf->wl_surface);
}


static void
nk_cairo_buffer_release(void *data,
			struct wl_buffer *wl_buffer)
{
	struct app_surface *surf = (struct app_surface *)data;
	for (int i = 0; i < 2; i++)
		if (surf->wl_buffer[i] == wl_buffer) {
			surf->dirty[i] = false;
			surf->committed[i] = false;
			break;
		}
}



void
nk_cairo_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			  nk_wl_drawcall_t draw_cb, struct shm_pool *pool,
			  uint32_t w, uint32_t h, uint32_t x, uint32_t y)
{
	nk_wl_impl_app_surface(surf, bkend, draw_cb, w, h, x, y);
	surf->pool = pool;
	for (int i = 0; i < 2; i++) {
		surf->wl_buffer[i] = shm_pool_alloc_buffer(pool, w, h);
		surf->dirty[i] = NULL;
		surf->committed[i] = NULL;
		shm_pool_set_buffer_release_notify(surf->wl_buffer[i],
						   nk_cairo_buffer_release, surf);
	}
	//also you need to create two wl_buffers
}

static struct nk_cairo_backend sample_backend;

static float
void_text_width_calculation(nk_handle handle, float height, const char *text, int len)
{
	return len * height / 2.0;
}

struct nk_wl_backend *
nk_cairo_create_bkend(void)
{
	nk_init_default(&sample_backend.base.ctx, &sample_backend.user_font);
	sample_backend.user_font.height = 16;
	sample_backend.user_font.width = void_text_width_calculation;

	return &sample_backend.base;
}
