/*
 * widget.c - taiwins client shell widget functions
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <wayland-util.h>
#include <os/file.h>

#include <ui.h>
#include "widget.h"


/************** The sample widgets *******************/
extern struct shell_widget clock_widget;
extern struct shell_widget what_up_widget;
extern struct shell_widget battery_widget;
extern void shell_widget_release_with_runtime(struct shell_widget *widget);

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
redraw_panel_for_dev(struct tw_event *e, int fd)
{
	struct shell_widget *widget = e->data;
	struct app_event ae = {
		.type = TW_TIMER,
		.time = widget->ancre.wl_globals->inputs.millisec,
	};
	widget->dev =
		tw_event_get_udev_device(e);
	widget->ancre.do_frame(&widget->ancre, &ae);
	udev_device_unref(widget->dev);
	widget->dev = NULL;
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
	/* int fd = open(path, O_RDONLY | O_CLOEXEC); */
	/* if (!fd) */
	/*	return; */
	/* //you don't need to set the fd here */
	/* widget->fd = fd; */
	/* //if mask is zero the client api will deal with a default flag */
	uint32_t mask = 0;

	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_file,
	};
	tw_event_queue_add_file(event_queue, path, &redraw_widget, mask);
	/* tw_event_queue_add_source(event_queue, fd, &redraw_widget, mask); */
}

static void
shell_widget_event_from_device(struct shell_widget *widget, const char *subsystem,
			       const char *dev, struct tw_event_queue *event_queue)
{
	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_dev,
	};
	tw_event_queue_add_device(event_queue, subsystem, dev,  &redraw_widget);
}

static bool
shell_widget_builtin(struct shell_widget *widget)
{
	return widget == &clock_widget ||
		widget == &what_up_widget  ||
		widget == &battery_widget;
}

const struct shell_widget *
shell_widget_get_builtin_by_name(const char *name)
{
	if (!strcmp("clock", name))
		return &clock_widget;
	if (!strcmp("whatup", name))
		return &what_up_widget;
	if (!strcmp("battery", name))
		return &battery_widget;
	return NULL;
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
	} else if (widget->subsystem && widget->devname)
		shell_widget_event_from_device(widget, widget->subsystem,
					       widget->devname, queue);
}

void
shell_widget_disactivate(struct shell_widget *widget, struct tw_event_queue *queue)
{
	app_surface_release(&widget->ancre);
	struct tw_event e = {
		.data = widget,
	};
	//remove those resources
	tw_event_queue_remove_source(queue, &e);
	//detect whether widget is a builtin widget
	if (shell_widget_builtin(widget))
		return;

	if (widget->file_path) {
		free(widget->file_path);
		widget->file_path = NULL;
	}
	if (widget->subsystem) {
		free(widget->subsystem);
		widget->subsystem = NULL;
	}
	if (widget->devname) {
		free(widget->devname);
		widget->devname = NULL;
	}
	//now you need to deference the luaState
	shell_widget_release_with_runtime(widget);
}

void
shell_widgets_load_default(struct wl_list *head)
{
	wl_list_insert(head, &clock_widget.link);
	wl_list_insert(head, &what_up_widget.link);
	//wl_list_insert(head, &battery_widget.link);//due to errors
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
