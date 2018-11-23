#ifndef SHELL_WIDGET_H
#define SHELL_WIDGET_H

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-client.h>
#include "ui.h"
#include "nuklear/nk_wl_egl.h"
#include "client.h"

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
	uint32_t label[64];
};

/**
 * @brief shell widget structure
 *
 * The structure supposed to be universal, and exposed to shell. For example,
 * panel takes the shell_widget structure, and it should be able to draw icons
 * with it. The shell should be able to launch the widget with it and so on.
 *
 * The widget takes the following assumptions:
 *
 * 1) the widget is an nuklear application, so it needs an nk_backend to start
 * it. nk_egl has launch function, so we do not need to keep an copy of
 * nk_egl_backend here.
 *
 * 2) the ancre is drawing on the parent surface as an sub nuklear application,
 * it does not need an surface.
 *
 * 3) the widget does not need to worry about start the widget as well. It is
 * done in the panel. because it does not know where to draw the widget.
 *
 * The widget, however, needs to know the size it occupies. In many case it does
 * not know in advance of this.
 *
 * Right now it is better, since the shell_widget event only has one thing, draw its parent!
 */
struct shell_widget {
	struct app_surface ancre;
	struct app_surface widget;
	struct wl_list link;
	nk_egl_draw_func_t ancre_cb;
	//widget callback
	nk_egl_draw_func_t draw_cb;
	nk_egl_postcall_t post_cb;
	//it could be lua state.
	void *user_data;
	//the effort to make it purely data
	struct timespec interval;
	char *file_path;
	uint32_t w;
	uint32_t h;
};


/* since right now the only event that widget has is draw its parent */
void shell_widget_event_from_timer(struct shell_widget *widget, struct timespec time,
				   struct tw_event_queue *event_queue);
void shell_widget_event_from_file(struct shell_widget *widget, const char *path,
				  struct tw_event_queue *event_queue);

void shell_widget_activate(struct shell_widget *widget, struct app_surface *panel, struct tw_event_queue *queue);

void shell_widget_launch(struct shell_widget *widget, struct wl_surface *surface, struct wl_proxy *p,
			 struct nk_egl_backend *bkend, uint32_t x, uint32_t y);


/* this is probably totally not necessary, we need only the script */
struct wl_list *shell_widget_create_with_funcs(nk_egl_draw_func_t draw_cb,
					       nk_egl_postcall_t post_cb,
					       nk_egl_draw_func_t update_cb,
					       size_t width, size_t height,
					       size_t scale);

struct wl_list *shell_widget_create_with_script(const char *script_content);



/************** The sample widgets *******************/
extern struct shell_widget clock_widget;

#ifdef __cplusplus
}
#endif


#endif
