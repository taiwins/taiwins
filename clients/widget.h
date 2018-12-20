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
#include "client.h"
#include "nk_backends.h"

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
	};
	//runtime access data
	int fd;
	void *user_data;

	uint32_t w;
	uint32_t h;
};

void shell_widget_activate(struct shell_widget *widget, struct app_surface *panel, struct tw_event_queue *queue);

static inline void
shell_widget_launch(struct shell_widget *widget, struct wl_surface *surface, struct wl_proxy *p,
		    struct nk_wl_backend *bkend, struct shm_pool *pool, uint32_t x, uint32_t y)
{
	struct wl_globals *globals = widget->widget.wl_globals;
	app_surface_init(&widget->widget, surface, p);
	widget->widget.wl_globals = globals;
	nk_cairo_impl_app_surface(&widget->widget, bkend, widget->draw_cb, pool, widget->w, widget->h, x, y);
	/* nk_egl_impl_app_surface(&widget->widget, bkend, widget->draw_cb, */
	/*			widget->w, widget->h, x, y); */
	app_surface_frame(&widget->widget, false);
}

/* this is probably totally not necessary, we need only the script */
struct wl_list *shell_widget_create_with_funcs(nk_wl_drawcall_t draw_cb,
					       nk_wl_postcall_t post_cb,
					       nk_wl_drawcall_t update_cb,
					       size_t width, size_t height,
					       size_t scale);

struct wl_list *shell_widget_create_with_script(const char *script_content);



/************** The sample widgets *******************/
extern struct shell_widget clock_widget;
extern struct shell_widget what_up_widget;
extern struct shell_widget battery_widget;

#ifdef __cplusplus
}
#endif


#endif
