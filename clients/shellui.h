#ifndef SHELL_UI_H
#define SHELL_UI_H

#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

//they have a list of the widget on the panel
struct shell_panel {
	struct app_surface panelsurf;
	enum wl_shm_format format;
	//in this case, you will also have a list of widgets
	vector_t widgets;
};

struct eglapp_icon {
	//you need to know the size of it
	cairo_surface_t *isurf;
	cairo_t *ctxt;
	void (*update_icon)(struct eglapp_icon *);
	//usually when you click it, we should be able to start the application
//	void (*pointron)(struct app_surface *surf);
	//left is true, right is false
//	void (*pointrbtn)(struct app_surface *surf);
	struct bbox box; //the location of the icon in the panel
};
struct eglapp;
//because you don't want to expose it, so we will have to create a lot help functions

struct eglapp *eglapp_from_icon(struct eglapp_icon *icon);
struct app_surface *appsurface_from_icon(struct eglapp_icon *icon);
struct bbox *icon_get_available_space(struct eglapp_icon *app);
struct eglapp_icon *icon_from_eglapp(struct eglapp *app);
struct app_surface *appsurface_from_app(struct eglapp *app);


void eglapp_init_with_funcs(struct eglapp *app,
			    void (*update_icon)(struct eglapp_icon *),
			    void (*draw_widget)(struct eglapp *));
void eglapp_init_with_script(struct eglapp *app, const char *script);

void
eglapp_launch(struct eglapp *app, struct egl_env *env, struct wl_compositor *compositor);


//TODO, remove this later.
void eglapp_update_icon(struct eglapp *app);


void eglapp_destroy(struct eglapp *app);

struct eglapp *
eglapp_addtolist(struct shell_panel *panel);


//sample function
void calendar_icon(struct eglapp_icon *icon);





#ifdef __cplusplus
}
#endif


#endif
