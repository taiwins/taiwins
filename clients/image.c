#include <cairo/cairo.h>
#include <wayland-client.h>

static cairo_format_t
translate_wl_shm_format(enum wl_shm_format format)
{
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
		return CAIRO_FORMAT_ARGB32;
		break;
	case WL_SHM_FORMAT_RGB888:
		return CAIRO_FORMAT_RGB24;
		break;
	case WL_SHM_FORMAT_RGB565:
		return CAIRO_FORMAT_RGB16_565;
		break;
	case WL_SHM_FORMAT_RGBA8888:
		return CAIRO_FORMAT_INVALID;
		break;
	default:
		return CAIRO_FORMAT_INVALID;
	}
}

/** load image to texture and free all the context */
unsigned char *
load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data)
{
	cairo_format_t format = translate_wl_shm_format(wlformat);
	if (format == CAIRO_FORMAT_INVALID)
		return NULL;
	cairo_t *pngcr, *memcr;
	cairo_surface_t *pngsurface = cairo_image_surface_create_from_png(path);
	int stride = cairo_format_stride_for_width(format, width);
	cairo_surface_t *memsurface = cairo_image_surface_create_for_data(data, format, width, height, stride);
	memcr = cairo_create(memsurface);
	cairo_set_source_surface(memcr, pngsurface, 0, 0);
	//well, that should do it
	cairo_paint(memcr);
	return data;
}
