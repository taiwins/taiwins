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
	char label[256];
};
typedef int (*shell_widget_draw_label_t)(struct shell_widget *, struct shell_widget_label *);

//free it afterwards
typedef int (*shell_widget_path_find_t)(struct shell_widget *, char *path);

/**
 * @brief shell widget structure
 *
 * the icons is drawn into unicodes.
 * The beautiful thing about shell_widget is that it is pure data.
 */
struct shell_widget {
	struct app_surface ancre;
	struct app_surface widget;
	struct wl_list link;
	shell_widget_draw_label_t ancre_cb;
	//widget callback
	nk_wl_drawcall_t draw_cb;
	nk_wl_postcall_t post_cb;
	//it could be lua state.
	//either a timer, or a static/dynamic path
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

static inline void
shell_widget_hook_panel(struct shell_widget *widget, struct app_surface *panel)
{
	embeded_impl_app_surface(&widget->ancre, panel, make_bbox_origin(0, 0, 1));
}



/************** The sample widgets *******************/
extern struct shell_widget clock_widget;
extern struct shell_widget what_up_widget;
extern struct shell_widget battery_widget;

#ifdef __cplusplus
}
#endif


#endif
