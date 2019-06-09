#include <cairo.h>


int main(int argc, char *argv[])
{
	cairo_surface_t *img_surf = cairo_image_surface_create_from_png(argv[1]);

	cairo_surface_t *draw_image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 100, 100);
	cairo_t *cr = cairo_create(draw_image);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	//paint will paint everything
	cairo_paint(cr);
	if (0) {
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
		cairo_set_line_width(cr, 10);

		// cairo matrix is an affine transformation matrix.
		//[xx xy x0;
		// yx yy y0;
		// 0  0  1 ]

		// in this 2D case, it is a forward matrix
		// so if I draw
		cairo_translate(cr, 20, 20);
		cairo_scale(cr, 0.5, 0.5);
		cairo_rectangle(cr, 0, 0, 100, 100);
		cairo_stroke(cr);
	}
	if (1) {
		cairo_pattern_t *pat = cairo_pattern_create_for_surface(img_surf);
		cairo_rectangle(cr, 0, 0, 100, 100);
		cairo_scale(cr, 100.0/cairo_image_surface_get_width(img_surf),
			    100.0/cairo_image_surface_get_height(img_surf));
		cairo_set_source(cr, pat);
		/* cairo_paint(cr); */
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	//when we do some thing with this
	cairo_surface_write_to_png(draw_image, argv[2]);
	cairo_destroy(cr);
	cairo_surface_destroy(draw_image);
	cairo_surface_destroy(img_surf);
	return 0;
}
