#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
//#include <librsvg/rsvg.h>
#include <cairo/cairo.h>

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

/*
int main(int argc, char *argv[])
{
	int width = 400;
	int height = 200;
	cairo_format_t format = CAIRO_FORMAT_ARGB32;
	int stride = cairo_format_stride_for_width(format, width);
	unsigned char content[height * stride];
	memset(content, 255, height * stride);
	cairo_surface_t *target = cairo_image_surface_create_for_data(content, format, width, height, stride);
	cairo_t *cr = cairo_create(target);

	cairo_surface_t *font = rendertext("text");
	cairo_set_source_surface(cr, font, 20, 100);
	cairo_paint(cr);
	cairo_clean(cr, 20, 100, 16 * strlen("text"), 32);
	cairo_surface_t *another_text = rendertext("another");
	cairo_set_source_surface(cr, another_text, 20, 100);
	cairo_paint(cr);
	cairo_surface_destroy(another_text);
	cairo_clean(cr, 20, 100, 16 * strlen("another"), 32);
	font = rendertext("abcdefg");
	cairo_set_source_surface(cr, font, 20, 100);
	cairo_paint(cr);
//	cairo_clean(cr, 20, 100, 16 * strlen("another"), 32);
	cairo_surface_write_to_png(target, "/tmp/debug.png");
	cairo_surface_destroy(font);
	cairo_surface_destroy(target);

	return 0;
}
*/

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
	int stride = cairo_format_stride_for_width(format, width);
	unsigned char content[height * stride];
	memset(content, 255, height * stride);
	cairo_surface_t *surface = cairo_image_surface_create_for_data(content, format, width, height, stride);
	cairo_t *cr = cairo_create(surface);


	char text[] = "we should see a cursor at the end.M";
	cairo_surface_t *font_surf = rendertext_with_caret(text, 16);
	cairo_set_source_surface(cr, font_surf, 20, 100);
	cairo_paint(cr);
	cairo_surface_write_to_png(surface, "/tmp/debug.png");
//	cairo_set_source_surface(cr, font, 20, 100);

	cairo_destroy(cr);
	cairo_surface_destroy(font_surf);
	cairo_surface_destroy(surface);
}
