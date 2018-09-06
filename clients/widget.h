#ifndef SHELL_WIDGET_H
#define SHELL_WIDGET_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>


#ifdef __cplusplus
extern "C" {
#endif
#include "ui.h"
#include "nk_wl_egl.h"



struct shell_widget {
	//anchor is a small surface that always showing
	struct app_surface ancre;
	struct app_surface widget;
	struct nk_egl_backend *bkend;

	nk_egl_draw_func_t draw_cb;
	nk_egl_postcall_t post_cb;
	appsurf_draw_t peignez_ancre;
};




#ifdef __cplusplus
}
#endif


#endif
