#ifndef SHELL_UI_H
#define SHELL_UI_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>

#include "ui.h"

#include <wayland-taiwins-shell-client-protocol.h>
extern struct taiwins_shell *shelloftaiwins;

#include "nk_wl_egl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct shell_widget;

//they have a list of the widget on the panel
struct shell_panel {
	//the resources
	struct app_surface panelsurf;
	struct app_surface widget_surface;

	//the anchor of the widget is the icon.
	vector_t widgets;
	struct shell_widget *active_one;
	struct nk_egl_backend *backend;
};

//panel widget apis
struct shell_widget *shell_panel_find_widget_at_xy(struct shell_panel *p, int xy, int y);
int shell_panel_n_widgets(struct shell_panel *p);
int shell_panel_paint_widget(struct shell_panel *panel, struct shell_widget *widget);
struct shell_widget *shell_panel_add_widget(struct shell_panel *surf);
void shell_panel_destroy_widgets(struct shell_panel *panel);

//panel_shell_ralated functions, the contructor and destructor are static, so we
//don't define them here
void shell_panel_show_widget(struct shell_panel *panel, int x, int y);
void shell_panel_hide_widget(struct shell_panel *panel);


struct eglapp_icon {
	//you need to know the size of it
	cairo_surface_t *isurf;
	cairo_t *ctxt;
	struct bbox box; //the location of the icon in the panel
	void (*update_icon)(struct eglapp_icon *);
};

#ifndef NK_EGLAPP_TEXT_MAX
#define NK_EGLAPP_TEXT_MAX 256
#endif

struct shell_widget {
	struct shell_panel *panel;
	//the awkward part is here, the anchor is in the icon
	struct eglapp_icon icon;
	bool ready_to_launch;
	//for native app, the function is defined as following.
	void (*draw_widget)(struct shell_widget *);
	//or, we will need a lua function to do it.
	lua_State *L;

	unsigned int text[NK_EGLAPP_TEXT_MAX];
	unsigned int text_len;
	uint32_t width, height;

};
//this will call the lua state of the widget
void draw_lua_widget(struct shell_widget *widget);


//later on you will need to use these function to build apps
void shell_widget_init_with_funcs(struct shell_widget *app,
			    void (*update_icon)(struct eglapp_icon *),
			    void (*draw_widget)(struct shell_widget *));
void shell_widget_init_with_script(struct shell_widget *app,
			     const char *script);


//the function that we used for events. We should have it later
int update_icon_event(void *data);

//the one sample
void calendar_icon(struct eglapp_icon *icon);
#ifdef __cplusplus
}
#endif


#endif
