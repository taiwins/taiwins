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


#define NK_IMPLEMENTATION
#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128


#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY

#include <cairo/cairo.h>
#include "../client.h"
#include "../ui.h"
#include "nk_wl_internal.h"

static void
nk_cairo_render(struct wl_buffer *buffer, struct nk_wl_backend *bkend,
		int32_t w, int32_t h,
		cairo_format_t format)
{
	const struct nk_command *cmd = NULL;
	void *data = shm_pool_buffer_access(buffer);

	cairo_surface_t *csurface =
		cairo_image_surface_create_for_data(
			(unsigned char *)data, format, w, h,
			cairo_format_stride_for_width(format, w));
	cairo_t *cr = cairo_create(csurface);
	//1) clean this buffer using its background color, or maybe nuklear does
	//that already

	cairo_set_source_rgb(cr, bkend->main_color.r, bkend->main_color.g,
			     bkend->main_color.b);
	cairo_paint(cr);

	//it is actually better to implement a table look up than switch command
	nk_foreach(cmd, &bkend->ctx) {
		switch (cmd->type) {
		case NK_COMMAND_NOP:
			fprintf(stderr, "cairo: no nuklear operation\n");
			break;
		}
	}

	cairo_destroy(cr);
	cairo_surface_destroy(csurface);

}

static void
nk_wl_render(struct nk_wl_backend *bkend)
{
	struct app_surface *surf = bkend->app_surface;
	//selecting the free frame
	struct wl_buffer *free_buffer = NULL;
	bool *dirty = NULL; bool *committed = NULL;
	int32_t w, h;

	w = surf->w; h = surf->h;
	for (int i = 0; i < 2; i++) {
		if (surf->committed[i] || surf->dirty[i])
			continue;
		free_buffer = surf->wl_buffer[i];
		dirty = &surf->dirty[i];
		committed = &surf->committed[i];
		break;
	}
	if (!free_buffer)
		return;
	if (!nk_wl_need_redraw(bkend))
		return;

	//otherwise can start the draw call now
	nk_cairo_render(free_buffer, bkend, w, h,
			translate_wl_shm_format(surf->pool->format));
}
