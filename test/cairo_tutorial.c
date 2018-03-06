#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <librsvg/rsvg.h>
#include <cairo/cairo.h>


//we try out several painting patterns then
cairo_surface_t * rendertext(const char *text)
{
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8 * strlen(text), 16);
	cairo_t *cr = cairo_create(surface);
	cairo_text_extents_t extent;
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 13);
	cairo_text_extents(cr, text, &extent);
	fprintf(stderr, "text extends (%f, %f)\n", extent.height, extent.width);
	cairo_move_to(cr, 25 - extent.width /2, 8 + extent.height/2);
	cairo_show_text(cr, text);
//	cairo_surface_write_to_png(surface, "/tmp/debug1.png");
	cairo_destroy(cr);
	return surface;
}


int main(int argc, char *argv[])
{
	int width = 100;
	int height = 200;
	cairo_format_t format = CAIRO_FORMAT_ARGB32;
	int stride = cairo_format_stride_for_width(format, width);
	unsigned char content[height * stride];
	memset(content, 255, height * stride);
	cairo_surface_t *target = cairo_image_surface_create_for_data(content, format, width, height, stride);
	cairo_surface_t *font = rendertext("text");
	cairo_t *cr = cairo_create(target);
	cairo_set_source_surface(cr, font, 20, 100);
	cairo_paint(cr);
	cairo_rectangle(cr, 20, 100, 50, 16);
	//it doesn't override the the content if you use 0 on alpha
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
	cairo_paint(cr);
	cairo_surface_t *another_text = rendertext("another");
	cairo_set_source_surface(cr, another_text, 20, 100);
	cairo_paint(cr);
	cairo_surface_write_to_png(target, "/tmp/debug.png");
	cairo_surface_destroy(target);
	cairo_surface_destroy(font);
	return 0;
}
