#include <stdio.h>
#include <string.h>
#include <wayland-util.h>
#include "ui.h"
#include "widget.h"
#include "../3rdparties/iconheader/IconsFontAwesome5.h"

static int
redraw_panel(void *data)
{
	struct app_surface *ancre = data;
	//panel gets redrawed for once we have a event
	ancre->do_frame(ancre, 0);
	return true;
}


static void
shell_widget_event_from_timer(struct shell_widget *widget, struct timespec time,
			      struct tw_event_queue *event_queue)
{
	struct tw_event redraw_widget = {
		.data = &widget->ancre,
		.cb = redraw_panel,
	};
	tw_event_queue_add_timer(event_queue, &time, &redraw_widget);
}


static void
shell_widget_event_from_file(struct shell_widget *widget, const char *path,
			     struct tw_event_queue *event_queue)
{
	//dude, what the hell is this?
	int fd = 0;
	uint32_t mask = 0;
	//TODO, open the file and set the mask

	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel,
	};
	tw_event_queue_add_source(event_queue, fd, &redraw_widget, mask);
}


void
shell_widget_activate(struct shell_widget *widget, struct app_surface *panel, struct tw_event_queue *queue)
{

	if (widget->interval.tv_sec || widget->interval.tv_nsec)
		shell_widget_event_from_timer(widget, widget->interval, queue);
	else if (widget->file_path)
		shell_widget_event_from_file(widget, widget->file_path, queue);
	else if (widget->path_find) {
		int len = widget->path_find(widget, NULL);
		char path[len+1];
		widget->path_find(widget, path);
		shell_widget_event_from_file(widget, path, queue);
	}
	//the size of the ancre here is irrelevant
	embeded_impl_app_surface(&clock_widget.ancre, panel, 0, 0, 0, 0);
}




/******************************* The sample widgets *********************************/

/*
 * This is a simple illustration of how to work with a row. We may really does
 * not know how much space the widget occupies inadvance.
 *
 * the widget should start with nk_layout_row_push to occupy its space, we limit
 * one widget right now, as it is what most time is. But we may need to have
 * more versatile approach
 */
static int
clock_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	//it seems to work, we still need a library to figure out the text size
	/* nk_layout_row_push(ctx, 100); */
	static const char * daysoftheweek[] =
		{"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);

	return snprintf(label->label, 255, "%s %02d:%02d:%02d",
		       daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min, tim->tm_sec);
}

static void
clock_widget_sample(struct nk_context *ctx, float width, float height, struct app_surface *app)
{
	/* enum nk_buttons btn; */
	/* uint32_t sx, sy; */
	//TODO, change the draw function to app->draw_widget(app);
	enum {EASY, HARD};
	static int op = EASY;
	static struct nk_text_edit text_edit;
	static bool init_text_edit = false;
	static char text_buffer[256];
	if (!init_text_edit) {
		init_text_edit = true;
		nk_textedit_init_fixed(&text_edit, text_buffer, 256);
	}

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, 30, 2);
	if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
	if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;

	nk_layout_row_dynamic(ctx, 25, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &text_edit, nk_filter_default);
}

struct shell_widget clock_widget = {
	.ancre_cb = clock_widget_anchor,
	.draw_cb = clock_widget_sample,
	.w = 200,
	.h = 150,
	.widget.s = 1,
	.interval = {
		.tv_sec = 1,
		.tv_nsec = 0,
	},
	.file_path = NULL,
};

static int
battery_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{

}

int
battery_sysfile_find(struct shell_widget *widget, char *path)
{
	const char *energy_now = "/sys/class/power_supply/BAT1/energy_now";
	if (path)
		strcpy(path, energy_now);
	return strlen(energy_now);
}


struct shell_widget battery_widget = {
	.ancre_cb = battery_anchor,
	.draw_cb = NULL,
	.w = 200,
	.h = 150,
	.path_find = battery_sysfile_find,
	.widget.s = 1,
	.interval = {0},
	.file_path = NULL,
};
