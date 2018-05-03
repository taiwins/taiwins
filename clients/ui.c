#include <stdio.h>
#include <cairo/cairo.h>
#include <wayland-client.h>

cairo_format_t
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

size_t
stride_of_wl_shm_format(enum wl_shm_format format)
{
	switch(format) {
	case WL_SHM_FORMAT_ARGB8888:
		return 4;
		break;
	case WL_SHM_FORMAT_RGB888:
		return 3;
		break;
	case WL_SHM_FORMAT_RGB565:
		return 2;
		break;
	case WL_SHM_FORMAT_RGBA8888:
		return 4;
		break;
	case WL_SHM_FORMAT_ABGR1555:
		return 2;
	default:
		return 0;
	}
}

/** load image to texture and free all the context */
unsigned char *
load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data)
{
	cairo_format_t format = translate_wl_shm_format(wlformat);
	if (format == CAIRO_FORMAT_INVALID)
		goto err_format;
	cairo_t *memcr;
	cairo_surface_t *pngsurface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(pngsurface) != CAIRO_STATUS_SUCCESS)
		goto err_loadimg;

	int stride = cairo_format_stride_for_width(format, width);
	cairo_surface_t *memsurface = cairo_image_surface_create_for_data(data, format, width, height, stride);
	memcr = cairo_create(memsurface);
	//lol, I need to scale before I set the source
	fprintf(stderr, "we got image with dimension %d %d\n",
		cairo_image_surface_get_width(pngsurface), cairo_image_surface_get_height(pngsurface));
	cairo_scale(memcr, (double)width / cairo_image_surface_get_width(pngsurface) ,
		    (double)height / cairo_image_surface_get_height(pngsurface));
	cairo_set_source_surface(memcr, pngsurface, 0, 0);
	cairo_paint(memcr);
	//TODO, free memory
	cairo_destroy(memcr);
	cairo_surface_destroy(pngsurface);
	cairo_surface_destroy(memsurface);
	return data;

err_loadimg:
	cairo_surface_destroy(pngsurface);
err_format:
	return NULL;
}
