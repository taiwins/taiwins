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
//this will pull the freetype headers
#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

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
#define NK_CR_FONT_KEY ((cairo_user_data_key_t *)1000)


struct nk_cairo_font {
	int size;
	float scale;
	FT_Face text_font;
	FT_Face icon_font;
	FT_Library ft_lib;
	struct nk_user_font nk_font;
};


static void
nk_cairo_font_done(struct nk_cairo_font *user_font)
{
	FT_Done_Face(user_font->text_font);
	FT_Done_Face(user_font->icon_font);
	FT_Done_FreeType(user_font->ft_lib);
}

static inline bool
is_in_pua(nk_rune rune)
{
	return rune >= 0xE000 && rune <= 0xF8FF;
}

static FT_Vector
nk_cairo_calc_advance(int32_t this_glyph, int32_t last_glyph,
		      FT_Face this_face, FT_Face last_face)
{
	FT_Vector advance_vec = {0, 0};
	if (this_face == last_face)
		FT_Get_Kerning(this_face, last_glyph, this_glyph,
			       ft_kerning_default, &advance_vec);

	FT_Load_Glyph(this_face, this_glyph, FT_LOAD_DEFAULT);
	advance_vec.x += this_face->glyph->advance.x;
	advance_vec.y += this_face->glyph->advance.y;
	return advance_vec;
}

//we need a fast way to calculate text width and rest is just rendering glyphs,
//since we already know how to
static float
nk_cairo_text_width(nk_handle handle, float height, const char *text,
		    int utf8_len)
{
	struct nk_cairo_font *user_font = handle.ptr;
	float width = 0.0;
	int len_decoded = 0;
	nk_rune ucs4_this;
	FT_Face curr_face, last_face = NULL;
	int32_t glyph_this, glyph_last = -1;

	while (len_decoded < utf8_len) {

		int num_bytes = nk_utf_decode(text + len_decoded,
					      &ucs4_this, utf8_len - len_decoded);
		len_decoded += num_bytes;
		curr_face = is_in_pua(ucs4_this) ?
			user_font->icon_font :
			user_font->text_font;
		glyph_this = FT_Get_Char_Index(curr_face, ucs4_this);
		FT_Vector real_advance = nk_cairo_calc_advance(glyph_this, glyph_last,
							       curr_face, last_face);

		width += real_advance.x / 64.0;

		glyph_last = glyph_this;
		last_face = curr_face;
	}
	return width;
}

static int
utf8_to_ucs4(nk_rune *runes, const int ucs4_len, const char *text, const int utf8_len)
{
	int len_decoded = 0;
	int i = 0;
	while (len_decoded < utf8_len && i < ucs4_len) {
		len_decoded += nk_utf_decode(text+len_decoded, &runes[i++], utf8_len - len_decoded);
	}
	return i;
}

static bool
nk_cairo_is_whitespace(nk_rune code) {
	bool ws;
	switch (code) {
	case ' ':
	case '\t':
	case '\v':
	case '\f':
	case '\r':
		ws = true;
		break;
	default:
		ws = false;
	}
	return ws;
}


static struct nk_vec2
nk_cairo_render_glyph(cairo_t *cr, FT_Face face, const nk_rune codepoint,
		      const int32_t glyph, const struct nk_vec2 *baseline)
{
	cairo_matrix_t matrix;
	FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT);
	struct nk_vec2 advance = { face->glyph->metrics.horiAdvance / 64.0, 0 };
	if (nk_cairo_is_whitespace(codepoint))
		return advance;

	FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	int w = bitmap->width;
	int h = bitmap->rows;
	//make glyph and pattern
	uint32_t pixels[w * h * sizeof(uint32_t)];
	for (int i = 0; i < w * h; i++) {
		pixels[i] = bitmap->buffer[i] << 24;

	}
	//now make pattern out of it
	cairo_surface_t *gsurf =
		cairo_image_surface_create_for_data((unsigned char *)pixels, CAIRO_FORMAT_ARGB32, w, h,
						    cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w));
	cairo_pattern_t *pattern = cairo_pattern_create_for_surface(gsurf);
	cairo_matrix_init_identity(&matrix);
	cairo_matrix_scale(&matrix, w, h);
	cairo_pattern_set_matrix(pattern, &matrix);

	{
		//horibearingY is a postive value
		struct nk_vec2 render_point = *baseline;
		float ybearing = face->glyph->metrics.horiBearingY / 64.0;
		float xbearing = face->glyph->metrics.horiBearingX / 64.0;
		render_point.y -= ybearing;
		render_point.x += xbearing;

		cairo_save(cr);
		cairo_translate(cr, render_point.x, render_point.y);
		cairo_scale(cr, w, h);
		/* cairo_rectangle(cr, 0, 0, 1, 1); */
		cairo_mask(cr, pattern);
		cairo_fill(cr);
		cairo_restore(cr);
	}

	cairo_pattern_destroy(pattern);
	cairo_surface_destroy(gsurf);

	return advance;
}

static void
nk_cairo_render_text(cairo_t *cr, const struct nk_vec2 *pos,
		     struct nk_cairo_font *font, const char *text, const int utf8_len)
{
	//convert the text to unicode
	//since we render horizontally, we only need to care about horibearingX,
	//horibearingY, horiAdvance
	//About the Pixel size

	//the baseline is actually the EM size, this is rather confusing, when
	//you set the pixel size 16. It set the pixel size of 'M', but the 'M'
	//has only ascent, it is already 16. for character like G, it has
	//descent, but smaller ascent, so the actual size of string is the
	//height, which is `18.625`.
	struct nk_vec2 advance = nk_vec2(pos->x, pos->y+font->size);
	nk_rune ucs4_this;
	FT_Face curr_face, last_face = NULL;
	int32_t glyph_this, glyph_last = -1;


	nk_rune unicodes[utf8_len];
	int nglyphs = utf8_to_ucs4(unicodes, utf8_len, text, utf8_len);
	if (nglyphs == 0)
		return;

	for (int i = 0; i < nglyphs; i++) {
		FT_Vector kern_vec = {0, 0};
		ucs4_this = unicodes[i];
		curr_face = is_in_pua(ucs4_this) ? font->icon_font : font->text_font;
		glyph_this = FT_Get_Char_Index(curr_face, unicodes[i]);
		if (curr_face == last_face)
			FT_Get_Kerning(curr_face, glyph_last, glyph_this, FT_KERNING_DEFAULT, &kern_vec);
		//we want to jump over white space as well
		//kerning, so we may move back a bit
		advance.x += kern_vec.x/64.0;
		advance.y += kern_vec.y/64.0;

		//now do the rendering
		struct nk_vec2 glyph_advance =
			nk_cairo_render_glyph(cr, curr_face, ucs4_this,
					      glyph_this, &advance);
		advance.x += glyph_advance.x;
		advance.y += glyph_advance.y;

		//rolling forward the state
		glyph_last = glyph_this;
		last_face = curr_face;
	}
}

static void
nk_cairo_font_set_size(struct nk_cairo_font *font, int pix_size, float scale)
{
	font->size = pix_size * scale;
	font->nk_font.height = pix_size * scale * 96.0 / 72.0;
	font->scale = scale;
	int error;
	error = FT_Set_Pixel_Sizes(font->text_font, 0, pix_size * scale);
	error = FT_Set_Pixel_Sizes(font->icon_font, 0, pix_size * scale);
	(void)error;

}

void
nk_cairo_font_init(struct nk_cairo_font *font, const char *text_font, const char *icon_font)
{
	int error;
	FT_Init_FreeType(&font->ft_lib);
	font->size = 0;
	int pixel_size = 16;
	font->size = pixel_size;
	font->nk_font.userdata.ptr = font;
	font->nk_font.width = nk_cairo_text_width;

//	char font_path[256];
	const char *vera = "/usr/share/fonts/TTF/Vera.ttf";
	const char *fa_a = "/usr/share/fonts/TTF/fa-regular-400.ttf";

	//tw_find_font_path(text_font, font_path, 256);
	error = FT_New_Face(font->ft_lib, vera, 0,
			    &font->text_font);

	if (FT_HAS_KERNING(font->text_font))
	    fprintf(stderr, "dejavu sans has kerning\n");
	//for the icon font, we need to actually verify the font charset, but
	//lets skip it for now
	//tw_find_font_path(icon_font, font_path, 256);
	error = FT_New_Face(font->ft_lib, fa_a, 0,
			    &font->icon_font);
	(void)error;
}

/////////////////////////////////// NK_CAIRO backend /////////////////////////////////////


struct nk_cairo_backend {
	struct nk_wl_backend base;

	nk_max_cmd_t last_cmds[2];
	struct nk_cairo_font user_font;
	//this is a stack we keep right now, all other method failed.
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
	cairo_matrix_t matrix;
	cairo_get_matrix(cr, &matrix);
	const struct nk_command_text *t =
		(const struct nk_command_text *)cmd;
	/* fprintf(stderr, "before rendering (xx:%f, yy:%f), (x0:%f, y0:%f)\t", */
	/*	matrix.xx, matrix.yy, matrix.x0, matrix.y0); */
	/* /\* fprintf(stderr, "render_pos: (%d, %d)\t", t->x, t->y); *\/ */
	/* fprintf(stderr, "str:%s   ", t->string); */
	/* fprintf(stderr, "color: (%d,%d,%d)\n", t->foreground.r, t->foreground.g, t->foreground.b); */
	cairo_set_source_rgb(cr, NK_COLOR_TO_FLOAT(t->foreground.r),
			     NK_COLOR_TO_FLOAT(t->foreground.g),
			     NK_COLOR_TO_FLOAT(t->foreground.b));
	/* nk_cairo_set_painter(cr, &t->foreground, 0); */
	struct nk_cairo_font *font = t->font->userdata.ptr;
	struct nk_vec2 rpos = nk_vec2(t->x, t->y);
	nk_cairo_render_text(cr, &rpos, font, t->string, t->length);
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
nk_cairo_render(struct wl_buffer *buffer, struct nk_cairo_backend *b,
		struct app_surface *surf, int32_t w, int32_t h)
{
	struct nk_wl_backend *bkend = &b->base;
	cairo_format_t format = translate_wl_shm_format(surf->pool->format);
	cairo_surface_t *image_surface =
		cairo_image_surface_create_for_data(
			shm_pool_buffer_access(buffer),
			format, w, h,
			cairo_format_stride_for_width(format, w));
	cairo_t *cr = cairo_create(image_surface);
	cairo_surface_destroy(image_surface);

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
	cairo_destroy(cr);

}


static void
nk_wl_render(struct nk_wl_backend *bkend)
{
	struct nk_cairo_backend *b =
		container_of(bkend, struct nk_cairo_backend, base);
	struct app_surface *surf = bkend->app_surface;
	struct wl_buffer *free_buffer = NULL;
	bool *to_commit = NULL;
	bool *to_dirty = NULL;

	for (int i = 0; i < 2; i++) {
		if (surf->committed[i] || surf->dirty[i])
			continue;
		free_buffer = surf->wl_buffer[i];
		to_commit = &surf->committed[i];
		to_dirty = &surf->dirty[i];
		break;
	}

	//selecting the free frame
	if (!nk_wl_need_redraw(bkend))
		return;
	if (!free_buffer)
		return;
	*to_dirty = true;

	nk_cairo_render(free_buffer, b, surf, surf->w, surf->h);
	wl_surface_attach(surf->wl_surface, free_buffer, 0, 0);
	wl_surface_damage(surf->wl_surface, 0, 0, surf->w, surf->h);
	wl_surface_commit(surf->wl_surface);
	*to_commit = true;
	*to_dirty = false;
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

static void
nk_cairo_destroy_app_surface(struct app_surface *app)
{
	struct nk_wl_backend *b = app->user_data;
	nk_wl_clean_app_surface(b);
	app->user_data = NULL;
	for (int i = 0; i < 2; i++) {
		shm_pool_buffer_free(app->wl_buffer[i]);
		app->dirty[i] = false;
		app->committed[i] = false;
	}
	app->pool = NULL;
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
	surf->destroy = nk_cairo_destroy_app_surface;
	//also you need to create two wl_buffers
}


struct nk_wl_backend *
nk_cairo_create_bkend(void)
{
	struct nk_cairo_backend *b = malloc(sizeof(struct nk_cairo_backend));

	nk_init_fixed(&b->base.ctx, b->base.ctx_buffer, NK_MAX_CTX_MEM, &b->user_font.nk_font);
	nk_cairo_font_init(&b->user_font, NULL, NULL);
	nk_cairo_font_set_size(&b->user_font, 16, 1.0);
	return &b->base;
}


void
nk_cairo_destroy_bkend(struct nk_wl_backend *bkend)
{
	struct nk_cairo_backend *b =
		container_of(bkend, struct nk_cairo_backend, base);
	nk_cairo_font_done(&b->user_font);
	nk_free(&bkend->ctx);
}
