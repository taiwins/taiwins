#ifndef SHELL_WIDGET_H
#define SHELL_WIDGET_H

#include <stdlib.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-client.h>
#include "ui.h"
#include "nk_wl_egl.h"
#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif

struct shell_widget;
typedef void (*set_widget_event_cb)(struct shell_widget *widget, struct nk_egl_backend *ancre_backend,
				    struct tw_event_queue *queue);

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
 */
struct shell_widget {
	struct app_surface ancre;
	//anchor is a small surface that always showing
	struct app_surface widget;

	struct wl_list link;
	nk_egl_draw_func_t ancre_cb;
	//widget callback
	nk_egl_draw_func_t draw_cb;
	nk_egl_postcall_t post_cb;
	//the event an widget may watch on:
	set_widget_event_cb set_event_cb;
	void *user_data;

};

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
