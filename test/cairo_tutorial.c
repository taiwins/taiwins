#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
//#include <librsvg/rsvg.h>
#include <cairo/cairo.h>


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
	font = rendertext("asdfsdas");
	cairo_set_source_surface(cr, font, 20, 100);
	cairo_paint(cr);
//	cairo_clean(cr, 20, 100, 16 * strlen("another"), 32);
	cairo_surface_write_to_png(target, "/tmp/debug.png");
	cairo_surface_destroy(font);
	cairo_surface_destroy(target);

	return 0;
}
