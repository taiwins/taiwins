#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
//#include <librsvg/rsvg.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#define NK_IMPLEMENTATION

#include "../3rdparties/nuklear/nuklear.h"

static inline int
font_pt2px(int pt_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (ppi / 72.0 * pt_size);
}

static inline int
font_px2pt(int px_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (72.0 * px_size / ppi);
}


//we try out several painting patterns then
static cairo_surface_t * rendertext(const char *text)
{
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16 * strlen(text), 32);
	cairo_t *cr = cairo_create(surface);
	cairo_text_extents_t extent;
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 18);
	cairo_text_extents(cr, text, &extent);
	fprintf(stderr, "text extends (%f, %f), and surface (%ld, %d)\n",
		extent.width, extent.height, 16 * strlen(text), 32);
	cairo_move_to(cr, 8 * strlen(text) - extent.width /2, 16 + extent.height/2);
	cairo_show_text(cr, text);
//	cairo_surface_write_to_png(surface, "/tmp/debug1.png");
	cairo_destroy(cr);
	return surface;
}

static void cairo_clean(cairo_t *context, double x, double y, double w, double h)
{
	cairo_rectangle(context, x, y, w, h);
	cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 1.0);
	cairo_paint(context);
}


//we try out several painting patterns then
static cairo_surface_t *
rendertext_with_caret(const char *text, int font_size)
{
	int pix_size = font_pt2px(font_size, 96);
	//it is probably bigger
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16 * strlen(text), pix_size);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0, 0, 0, 1.0);
	cairo_paint(cr);
	cairo_text_extents_t extent;
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 16);
	cairo_text_extents(cr, text, &extent);
	fprintf(stderr, "text extends (%f, %f), and surface is  (%ld, %d)\n",
		extent.width, extent.height, 16 * strlen(text), pix_size);
//	assert(extent.height == (double)pix_size);
	//I think they draw on that point
	cairo_move_to(cr, 0, extent.height);
	cairo_show_text(cr, text);
	cairo_move_to(cr, extent.width + 6.0, 0);
	cairo_line_to(cr, extent.width + 6.0, extent.height);
	cairo_set_line_width(cr, 6.0);
	cairo_stroke(cr);
//	cairo_surface_write_to_png(surface, "/tmp/debug1.png");
	cairo_destroy(cr);
	return surface;
}


/* luckly we know that cairo text extends */
int main(int argc, char *argv[])
{
	//create canvas and paint clean it
	int width = 400;
	int height = 200;
	cairo_format_t format = CAIRO_FORMAT_ARGB32;
	FT_Library library;
	FT_Face face;
	const cairo_user_data_key_t key;

	FT_Init_FreeType(&library);

	int stride = cairo_format_stride_for_width(format, width);
	unsigned char content[height * stride];
	memset(content, 255, height * stride);
	cairo_surface_t *surface = cairo_image_surface_create_for_data(content, format, width, height, stride);
	cairo_t *cr = cairo_create(surface);
	int error = FT_New_Face(library,
				"/usr/share/fonts/truetype/fa-regular-400.ttf",
				0, &face);
	cairo_font_face_t *font_face = cairo_ft_font_face_create_for_ft_face(face, 0);
	fprintf(stderr, "cairo_font face: %p\n", font_face);
	if (cairo_font_face_status(font_face) != CAIRO_STATUS_SUCCESS)
		fprintf(stderr, "oh shit, I screwed something up\n");
	cairo_set_font_face(cr, font_face);
	cairo_set_font_size(cr, 20);
	cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
	if (cairo_scaled_font_status(scaled_font) != CAIRO_STATUS_SUCCESS)
		fprintf(stderr, "oh shit, scaled font didn\'t work\n");

	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);
	unsigned int unicodes[] = {0xF1C1, 0xF1C2, 0xF1C3, 0xF1C4, 0xF1C5, 0xF1C6};
	char text[256];
	int count = 0;
	for (int i = 0; i < 6; i++) {
		count += nk_utf_encode(unicodes[i], text+count, 256 - count);
	}
	text[count] = '\0';

//	char text[] = "we should see a cursor at the end.M";
	cairo_move_to(cr, 100, 100);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	{
		int num_glyphs;
		cairo_glyph_t *glyphs;
		int num_clusters;
		cairo_text_cluster_t *clusters;
		cairo_text_cluster_flags_t flags;
		cairo_font_extents_t extents;
		cairo_scaled_font_extents(scaled_font, &extents);
		fprintf(stderr, "%d\n", extents.ascent);
		cairo_scaled_font_text_to_glyphs(scaled_font, 100, 100+extents.ascent,
						 text, count, &glyphs, &num_glyphs, &clusters, &num_clusters, &flags);
		cairo_show_text_glyphs(cr, text, count, glyphs, num_glyphs, clusters, num_clusters, flags);

		/* cairo_glyph_free(glyphs); */
		/* cairo_text_cluster_free(clusters); */
	}
	cairo_surface_write_to_png(surface, "/tmp/debug.png");
	cairo_destroy(cr);
//	cairo_surface_destroy(font_surf);

	cairo_surface_destroy(surface);
}
