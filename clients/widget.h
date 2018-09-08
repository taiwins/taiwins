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

#ifdef __cplusplus
extern "C" {
#endif


//if we want to allow only one widget at a time, we need to some
struct shell_widget {
	//anchor is a small surface that always showing
	struct app_surface ancre;
	struct app_surface widget;
	struct nk_egl_backend *bkend;

	struct wl_list link;
	nk_egl_draw_func_t draw_cb;
	nk_egl_postcall_t post_cb;
	appsurf_draw_t peignez_ancre;
};

struct wl_list *shell_widget_create_with_funcs(nk_egl_draw_func_t draw_cb,
					       nk_egl_postcall_t post_cb,
					       appsurf_draw_t anchor_cb,
					       size_t width, size_t height,
					       size_t scale);

struct wl_list *shell_widget_create_with_script(const char *script_content);




#ifdef __cplusplus
}
#endif


#endif
