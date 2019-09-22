#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <wayland-util.h>
#include <os/file.h>

#include <ui.h>
#include "widget.h"

static int
redraw_panel_for_file(struct tw_event *e, int fd)
{
	struct shell_widget *widget = e->data;
	struct app_event ae = {
		.type = TW_TIMER,
		.time = widget->ancre.wl_globals->inputs.millisec,
	};
	//you set it here it will never work
	widget->fd = fd;
	//panel gets redrawed for once we have a event
	widget->ancre.do_frame(&widget->ancre, &ae);
	//if somehow my fd changes, it means I no longer watch this fd anymore
	if (widget->fd != fd)
		return TW_EVENT_DEL;
	else
		return TW_EVENT_NOOP;
}

static int
redraw_panel_for_timer(struct tw_event *e, int fd)
{
	struct shell_widget *widget = e->data;
	struct app_event ae = {
		.type = TW_TIMER,
		.time = widget->ancre.wl_globals->inputs.millisec,
	};

	widget->fd = fd;
	widget->ancre.do_frame(&widget->ancre, &ae);
	//test if this is a one time event
	if (!(widget->interval).it_interval.tv_sec &&
	    !widget->interval.it_interval.tv_nsec)
		return TW_EVENT_DEL;
	else
		return TW_EVENT_NOOP;
}

static void
shell_widget_event_from_timer(struct shell_widget *widget, struct itimerspec *time,
			      struct tw_event_queue *event_queue)
{
	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_timer,
	};
	tw_event_queue_add_timer(event_queue, time, &redraw_widget);
}


static void
shell_widget_event_from_file(struct shell_widget *widget, const char *path,
			     struct tw_event_queue *event_queue)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (!fd)
		return;
	//you don't need to set the fd here
	widget->fd = fd;
	//if mask is zero the client api will deal with a default flag
	uint32_t mask = 0;

	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_file,
	};
	tw_event_queue_add_source(event_queue, fd, &redraw_widget, mask);
}


void
shell_widget_activate(struct shell_widget *widget, struct tw_event_queue *queue)
{
	if (widget->interval.it_value.tv_sec || widget->interval.it_value.tv_nsec)
		shell_widget_event_from_timer(widget, &widget->interval, queue);
	else if (widget->file_path)
		shell_widget_event_from_file(widget, widget->file_path, queue);
	else if (widget->path_find) {
		int len = widget->path_find(widget, NULL);
		if (len) {
			char path[len + 1];
			shell_widget_event_from_file(widget, path, queue);
		}
	}
}


/*******************************************************************************
 * sample widget
 ******************************************************************************/

static int
whatup_ancre(struct shell_widget *widget, struct shell_widget_label *label)
{
	strcpy(label->label, "what-up!");
	return 8;
}

struct shell_widget what_up_widget = {
	.ancre_cb = whatup_ancre,
	.draw_cb = NULL,
	.w = 200,
	.h = 150,
	.path_find = NULL,
	.interval = {{0},{0}},
	.file_path = NULL,
};
