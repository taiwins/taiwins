#include <stdint.h>
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

static void cairo_clean(cairo_t *context, double x, double y, double w, double h)
{
	cairo_rectangle(context, x, y, w, h);
	cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 1.0);
	cairo_paint(context);
}

static void
print_bitmap(unsigned char *bitmap, int width, int height, int pitch)
{
	char row[width+1];
	row[width] = '\0';
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++)
			row[j] = (bitmap[pitch * i + j] != 0) ? 'A' : ' ';
		fprintf(stderr, "%s\n", row);
	}
}

/* luckly we know that cairo text extends */
int main(int argc, char *argv[])
{
	int ft_err;
	int glyph_index;
	//create canvas and paint clean it
	int width = 400;
	int height = 200;
	cairo_format_t format = CAIRO_FORMAT_ARGB32;
	FT_Library library;
	FT_Face font_face;

	ft_err = FT_Init_FreeType(&library);
	FT_New_Face(library, "/usr/share/fonts/TTF/Vera.ttf", 0, &font_face);
	FT_Set_Pixel_Sizes(font_face, 0, 16);
	glyph_index = FT_Get_Char_Index(font_face, 'M');
	FT_Load_Glyph(font_face, glyph_index, FT_LOAD_DEFAULT);
	FT_Render_Glyph(font_face->glyph, FT_RENDER_MODE_NORMAL);
	FT_Bitmap *bitmap = &font_face->glyph->bitmap;
	assert(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
	int glyph_width = bitmap->width;
	int glyph_height = bitmap->rows;
	int glyph_stride = bitmap->pitch;
	uint32_t pixels[glyph_width * glyph_height];
	for (int i = 0; i< glyph_width * glyph_height; i++) {
		pixels[i] = bitmap->buffer[i] << 24;
	}

	cairo_surface_t *glyph_surface =
		cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32,
						    glyph_width, glyph_height,
						    cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, glyph_width));
	print_bitmap(bitmap->buffer, glyph_width, glyph_height, glyph_stride);

	/* fprintf(stderr, "glyph width: %d, height:%d\n", glyph_width, glyph_height); */
	/* cairo_pattern_t *glyph_pattern = cairo_pattern_create_for_surface(glyph_surface); */
	/* cairo_surface_write_to_png(glyph_surface, "/tmp/glyph.png"); */



	//I should create the
	int stride = cairo_format_stride_for_width(format, width);
	unsigned char content[height * stride];
	memset(content, 255, height * stride);
	cairo_surface_t *surface = cairo_image_surface_create_for_data(content, format, width, height, stride);
	cairo_matrix_t matrix;


	/* cairo_pattern_type_t p_type = cairo_pattern_get_type(p); */
	/* fprintf(stderr, "the pattern type is %d\n", p_type); */

	fprintf(stderr, "matrix scale (%f, %f), shift: (%f, %f)\n",
		matrix.xx, matrix.yy, matrix.x0, matrix.y0);

	cairo_t *cr = cairo_create(surface);

	cairo_pattern_t *p = cairo_pattern_create_for_surface(glyph_surface);
	cairo_pattern_type_t p_type = cairo_pattern_get_type(p);
	fprintf(stderr, "the pattern type is %d\n", p_type);

	cairo_matrix_init_identity (&matrix);
	cairo_matrix_scale (&matrix, glyph_width, glyph_height);
	cairo_pattern_set_matrix (p, &matrix);

	/* cairo_save(cr); */
	cairo_translate(cr, 100, 50);
	cairo_scale(cr, glyph_width, glyph_height);
	/* cairo_rectangle(cr, 10, 10, 100, 100); */
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, 1, 1);
	/* cairo_pattern_t *linpat = cairo_pattern_create_linear(0, 0, 1, 1); */
	/* cairo_pattern_add_color_stop_rgb(linpat, 0, 0.3, 0.3, 0.8); */
	/* cairo_pattern_add_color_stop_rgb(linpat, 1, 0.0, 0.8, 0.3); */
	/* cairo_fill(cr); */

	/* cairo_pattern_t *radpat = cairo_pattern_create_radial(0.5, 0.5, 0.25, 0.5, 0.5, 0.75); */
	/* cairo_pattern_add_color_stop_rgba (radpat, 0, 0, 0, 0, 1); */
	/* cairo_pattern_add_color_stop_rgba (radpat, 0.5, 0, 0, 0, 0); */
	/* cairo_set_source(cr, linpat); */
	/* cairo_rectangle(cr, 0, 0, 1, 1); */
	cairo_mask(cr, p);
	/* cairo_fill(cr); */
	/* cairo_mask_surface(cr, glyph_surface, 0.0, 0.0); */
	//rectangle is a type of pattern as well,
	/* cairo_rectangle(cr, 0.0, 0.0, 1.0, 1.0); */
	/* cairo_paint(cr); */
	/* cairo_mask(cr, glyph_pattern); */
	/* cairo_restore(cr); */

	cairo_surface_write_to_png(surface, "/tmp/debug.png");
	cairo_destroy(cr);
//	cairo_surface_destroy(font_surf);

	cairo_surface_destroy(surface);

	FT_Done_Face(font_face);
	FT_Done_FreeType(library);

}
