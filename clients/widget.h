/*
 * widget.h - taiwins client shell widget header
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

#ifndef SHELL_WIDGET_H
#define SHELL_WIDGET_H

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-client.h>
#include <libudev.h>
#include <ui.h>
#include <client.h>
#include <nk_backends.h>

#ifdef __cplusplus
extern "C" {
#endif

struct shell_widget;

/**
 * /brief icon data format
 *
 * So we have decided to convert icons all into unicode, so all the anchors gets
 * draw as glyphs.
 *
 * For this to work, we need a system to register svgs and upload it. Then map
 * it to its unicode
 */
struct shell_widget_label {
	char label[32];
};
typedef int (*shell_widget_draw_label_t)(struct shell_widget *, struct shell_widget_label *);
typedef int (*shell_widget_setup_cb_t)(struct shell_widget *);

//free it afterwards
typedef int (*shell_widget_path_find_t)(struct shell_widget *, char *path);

/**
 * @brief shell widget structure
 *
 * the icons is drawn into unicodes.
 * The beautiful thing about shell_widget is that it is pure data.
 */
struct shell_widget {
	struct taiwins_ui *proxy;
	struct tw_event_queue *queue;
	struct tw_appsurf ancre;
	struct tw_appsurf widget;
	struct wl_list link;
	shell_widget_draw_label_t ancre_cb;
	shell_widget_setup_cb_t setup_cb;
	nk_wl_drawcall_t draw_cb;
	//watchers
	struct {
		//a immediate/reccurent timer
		struct itimerspec interval;
		char *file_path;
		shell_widget_path_find_t path_find;
		char *subsystem;
		char *devname;
	};
	//runtime access data
	int fd;
	void *user_data;
	struct udev_device *dev;

	uint32_t w;
	uint32_t h;
};

void shell_widget_activate(struct shell_widget *widget, struct tw_event_queue *queue);

void shell_widget_disactivate(struct shell_widget *widget, struct tw_event_queue *queue);

void shell_widgets_load_default(struct wl_list *head);
void shell_widgets_load_script(struct wl_list *head, struct tw_event_queue *queue,
			       const char *path);

const struct shell_widget *shell_widget_get_builtin_by_name(const char *name);

static inline void
shell_widget_hook_panel(struct shell_widget *widget, struct tw_appsurf *panel)
{
	embeded_impl_app_surface(&widget->ancre, panel, tw_make_bbox_origin(0, 0, 1));
	if (widget->setup_cb)
		widget->setup_cb(widget);
}



#ifdef __cplusplus
}
#endif


#endif
