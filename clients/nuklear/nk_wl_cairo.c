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
#include <cairo/cairo-ft.h>


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

#include "../client.h"
#include "../ui.h"
#include "nk_wl_internal.h"

/////////////////////////////////// NK_CAIRO text handler /////////////////////////////////////

//every font_face has a user_data array, in our case, we just bind to this
//address
#define NK_CR_FONT_KEY ((cairo_user_data_key_t *)1000)

struct nk_cairo_font {
	//we need have a text font and an icon font, both of them should be FT_fontface
	cairo_font_face_t *font_face;
	cairo_scaled_font_t *font_using;
	struct nk_user_font nk_font;
	int size;
	FT_Face text_font;
	FT_Face icon_font;
};

static void
nk_cairo_font_done(void *data)
{
	struct nk_cairo_font *user_font = (struct nk_cairo_font *)data;
	FT_Done_Face(user_font->text_font);
	FT_Done_Face(user_font->icon_font);
}

static cairo_status_t
font_text_to_glyphs(
	cairo_font_face_t *font_face,
	const char *utf8,
	int utf8_len,
	cairo_glyph_t **glyphs,
	int *num_glyphs,
	cairo_text_cluster_t **clusters,
	int *num_clusters,
	cairo_text_cluster_flags_t *cluster_flags)
{
	struct nk_cairo_font *user_font =
		cairo_font_face_get_user_data(font_face, NK_CR_FONT_KEY);

	//get number of unicodes to
	nk_rune unicodes[utf8_len];
	int len_decoded = 0;
	int len = 0;
	do {
		len_decoded = nk_utf_decode(utf8 + len_decoded, unicodes + len,
					    utf8_len - len_decoded);
		len++;
	} while(len_decoded < utf8_len);

	//deal with kerning and all that
	nk_rune pua[2] = {0xE000, 0xF8FF};
	for (int i = 0; i < len; i++) {

	}
}

//text APIs
static cairo_status_t
scaled_font_text_to_glyphs(
	cairo_scaled_font_t *scaled_font,
	const char *utf8,
	int utf8_len,
	cairo_glyph_t **glyphs,
	int *num_glyphs,
	cairo_text_cluster_t **clusters,
	int *num_clusters,
	cairo_text_cluster_flags_t *cluster_flags)
{
	cairo_font_face_t *font_face = cairo_scaled_font_get_font_face(scaled_font);
	return font_text_to_glyphs(font_face, utf8, utf8_len, glyphs, num_glyphs,
				   clusters, num_clusters, cluster_flags);
}


static cairo_status_t
scaled_font_render_glyphs(cairo_scaled_font_t *scaled_font, unsigned long unicode,
			  cairo_t *cr, cairo_text_extents_t *extents)
{
	return CAIRO_STATUS_SUCCESS;
}

void
nk_cairo_font_init(struct nk_cairo_font *font, const char *text_font, const char *icon_font)
{
	int error;
	FT_Library library;
	FT_Init_FreeType(&library);


	char font_path[256];
	tw_find_font_path(text_font, font_path, 256);
	error = FT_New_Face(library, font_path, 0,
			    &font->text_font);
	//for the icon font, we need to actually verify the font charset, but
	//lets skip it for now
	tw_find_font_path(icon_font, font_path, 256);
	error = FT_New_Face(library, font_path, 0,
			    &font->icon_font);
	font->font_face = cairo_user_font_face_create();
	cairo_font_face_set_user_data(font->font_face, NK_CR_FONT_KEY, font,
				      nk_cairo_font_done);

	cairo_user_font_face_set_render_glyph_func(font->font_face,
						   scaled_font_render_glyphs);
	cairo_user_font_face_set_text_to_glyphs_func(font->font_face,
						     scaled_font_text_to_glyphs);

}

/////////////////////////////////// NK_CAIRO backend /////////////////////////////////////





struct nk_cairo_backend {
	struct nk_wl_backend base;

	nk_max_cmd_t last_cmds[2];
	struct nk_cairo_font user_font;
	//this is a stack we keep right now, all other method failed.
	struct {
		struct wl_buffer *buffer_using;
		cairo_t *cr_using;
		cairo_scaled_font_t *font_using;
	};
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
	nk_cairo_set_painter(cr, &t->foreground, 0);
	cairo_scaled_font_t *s = t->font->userdata.ptr;
	int num_glyphs;
	cairo_glyph_t *glyphs = NULL;
	int num_clusters;
	cairo_text_cluster_t *clusters = NULL;
	cairo_text_cluster_flags_t flags;
	cairo_font_extents_t extents;
	cairo_scaled_font_extents(s, &extents);
	cairo_scaled_font_text_to_glyphs(
		s, t->x, t->y+extents.ascent,
		t->string, t->length, &glyphs, &num_glyphs, &clusters, &num_clusters, &flags);
	cairo_show_text_glyphs(
		cr, t->string, t->length, glyphs, num_glyphs, clusters, num_clusters, flags);
	cairo_glyph_free(glyphs);
	cairo_text_cluster_free(clusters);
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
		int32_t w, int32_t h, cairo_t *cr)
{
	const struct nk_command *cmd = NULL;
	//1) clean this buffer using its background color, or maybe nuklear does
	//that already
	cairo_push_group(cr);
	cairo_set_source_rgb(cr, bkend->main_color.r, bkend->main_color.g,
			     bkend->main_color.b);
	cairo_paint(cr);

	//it is actually better to implement a table look up than switch command
	nk_foreach(cmd, &bkend->ctx) {
		nk_cairo_ops[cmd->type](cr, cmd);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint(cr);
	cairo_surface_flush(cairo_get_target(cr));

}

static void
nk_wl_call_preframe(struct nk_wl_backend *bkend, struct app_surface *surf)
{
	struct nk_cairo_backend *b =
		container_of(bkend, struct nk_cairo_backend, base);
	cairo_format_t format = translate_wl_shm_format(surf->pool->format);
	int32_t w, h;
	w = surf->w; h = surf->h;
	for (int i = 0; i < 2; i++) {
		if (surf->committed[i] || surf->dirty[i])
			continue;
		b->buffer_using = surf->wl_buffer[i];
		break;
	}
	if (!b->buffer_using)
		return;
	cairo_surface_t *image_surface =
		cairo_image_surface_create_for_data(
			shm_pool_buffer_access(b->buffer_using),
			format, w, h,
			cairo_format_stride_for_width(format, w));
	b->cr_using = cairo_create(image_surface);
	cairo_surface_destroy(image_surface);
	cairo_set_font_face(b->cr_using, b->user_font.font_face);
	cairo_set_font_size(b->cr_using, b->user_font.size);
	b->font_using = cairo_get_scaled_font(b->cr_using);
	b->user_font.nk_font.userdata.ptr = b->font_using;
}

static void
nk_wl_render(struct nk_wl_backend *bkend)
{
	struct nk_cairo_backend *b =
		container_of(bkend, struct nk_cairo_backend, base);
	struct app_surface *surf = bkend->app_surface;
	//selecting the free frame
	struct wl_buffer *free_buffer = b->buffer_using;
	cairo_t *cr = b->cr_using;
	bool *to_commit = free_buffer == (surf->wl_buffer[0]) ?
		&surf->committed[0] : &surf->committed[1];

	if (!free_buffer)
		return;
	if (!nk_wl_need_redraw(bkend))
		goto free_frame;

	nk_cairo_render(free_buffer, bkend, surf->w, surf->h, cr);
	wl_surface_attach(surf->wl_surface, free_buffer, 0, 0);
	wl_surface_damage(surf->wl_surface, 0, 0, surf->w, surf->h);
	wl_surface_commit(surf->wl_surface);
	*to_commit = true;
free_frame:
	cairo_destroy(b->cr_using);
	b->buffer_using = NULL;
	b->cr_using = NULL;
	b->font_using = NULL;
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

static float
nk_cairo_cal_text_width(nk_handle handle, float height, const char *text, int len)
{
	cairo_scaled_font_t *s = handle.ptr;
	cairo_glyph_t *glyphs = NULL;
	int nglyphs;
	cairo_text_extents_t extents;
	cairo_scaled_font_text_to_glyphs(s, 0, 0, text, len,
					 &glyphs, &nglyphs,
					 NULL, NULL, NULL);
	cairo_scaled_font_glyph_extents(s, glyphs, nglyphs, &extents);
	cairo_glyph_free(glyphs);
	return extents.x_advance;
}


static void
nk_cairo_prepare_font(struct nk_cairo_backend *bkend, const char *font_file)
{
	int error;
	FT_Library library;
	FT_Face face;
	cairo_font_face_t *font_face;
	/* static const cairo_user_data_key_t key; */

	FT_Init_FreeType(&library);
	//a font could have different face.
	error = FT_New_Face(library, font_file, 0, &face);
	font_face = cairo_ft_font_face_create_for_ft_face(face, 0);
	cairo_font_face_set_user_data(font_face, NK_CR_FONT_KEY, face,
				      (cairo_destroy_func_t)FT_Done_Face);
	bkend->user_font.font_face = font_face;
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


struct nk_wl_backend *
nk_cairo_create_bkend(void)
{
	nk_init_default(&sample_backend.base.ctx, &sample_backend.user_font.nk_font);
	sample_backend.user_font.nk_font.height = 16;
	sample_backend.user_font.nk_font.width = nk_cairo_cal_text_width;
	sample_backend.user_font.size = 14;

	char *font_path = "/usr/share/fonts/TTF/fa-regular-400.ttf";
//	tw_find_font_path("vera", font_path, 256);
//	fprintf(stderr, "%s\n", font_path);
	nk_cairo_prepare_font(&sample_backend, font_path);
	return &sample_backend.base;
}
