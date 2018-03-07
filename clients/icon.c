#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include "egl.h"
#include "shellui.h"
#include "client.h"

int update_icon_event(void *data)
{
	struct eglapp *app = (struct eglapp *)data;
	struct eglapp_icon *icon = icon_from_eglapp(app);
	icon->update_icon(icon);
	eglapp_update_icon(app);
	return 0;
};


const cairo_surface_t *
icon_from_svg(struct eglapp_icon *icon, const char *file)
{
	//create
	RsvgHandle *handle = rsvg_handle_new_from_file(file, NULL);
	rsvg_handle_render_cairo(handle, icon->ctxt);
	rsvg_handle_close(handle, NULL);
	return icon->isurf;
}

//sample icons routines
void
calendar_icon(struct eglapp_icon *icon)
{
	const struct bbox *avail_sp = icon_get_available_space(icon);
	static const char * daysoftheweek[] = {"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	char formatedtime[20];
	cairo_text_extents_t extent;
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);
	sprintf(formatedtime, "%s %02d:%02d:%02d",
		daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min, tim->tm_sec);
//	fprintf(stderr, "%s\n", formatedtime);
	int w = min(avail_sp->w, strlen(formatedtime) * avail_sp->h / 2);
	int h = avail_sp->h;
	//TODO find a way to choose the right font size
	if (icon->ctxt == NULL) {
		icon->isurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		icon->ctxt = cairo_create(icon->isurf);
	}
	//clean the source
	cairo_set_source_rgba(icon->ctxt, 1.0, 1.0f, 1.0f, 1.0f);
	cairo_paint(icon->ctxt);
	cairo_set_source_rgba(icon->ctxt, 0, 0, 0, 1.0);
	cairo_select_font_face(icon->ctxt, "sans",
			       CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(icon->ctxt, 12);
	cairo_text_extents(icon->ctxt, formatedtime, &extent);
//	fprintf(stderr, "the font rendered size (%f, %f) and (%d, %d)\n",
//		extent.width, extent.height, w, h);
	cairo_move_to(icon->ctxt, w/2 - extent.width/2 , h/2 + extent.height/2 );
	cairo_show_text(icon->ctxt, formatedtime);
//	cairo_surface_write_to_png(icon->isurf, "/tmp/debug.png");
}
