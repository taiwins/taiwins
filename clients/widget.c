#include <stdio.h>
#include <string.h>
#include <wayland-util.h>
#include "ui.h"
#include "widget.h"
#include "nk_wl_egl.h"

static int
redraw_panel(void *data)
{
	struct nk_egl_backend *backend = data;
	nk_egl_update(backend);
	return true;
}

static void
clock_widget_anchor(struct nk_context *ctx, float width, float height, void *data)
{
	nk_layout_row_push(ctx, 40);
	static const char * daysoftheweek[] =
		{"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	char formatedtime[20];
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);

	sprintf(formatedtime, "%s %02d:%02d:%02d",
		daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min, tim->tm_sec);

}

static void clock_set_timer(struct shell_widget *w,
			    struct nk_egl_backend *panel_backend,
			    struct tw_event_queue *event_queue)
{
	struct tw_event redraw_widget = {
		.data = panel_backend,
		.cb = redraw_panel,
	};

	struct timespec interval = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};
	tw_event_queue_add_timer(event_queue, &interval, &redraw_widget);
}


struct shell_widget clock_widget = {
	.set_event_cb = clock_set_timer,
	.ancre_cb = clock_widget_anchor,
};
