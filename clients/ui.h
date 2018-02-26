#ifndef TW_UI_H
#define TW_UI_H


#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif


unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);

enum APP_SURFACE_TYPE {
	APP_BACKGROUND,
	APP_PANEL,
	APP_WIDGET,
};

//this is the root structure by all the surfaces in the shell, others should
//extend on it. And wl_surface's usr data should point to it
struct app_surface {
	enum APP_SURFACE_TYPE type;
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	struct wl_buffer  *wl_buffer;
	//callbacks for wl_pointer and cursor
	void (*keycb)(struct app_surface *surf, xkb_keysym_t keysym);
	void (*pointron)(struct app_surface *surf);
	void (*pointrbtn)(struct app_surface *surf);


};

static inline struct app_surface *
app_surface_from_wl_surface(struct wl_surface *s)
{
	return (struct app_surface *)wl_surface_get_user_data(s);
}





#ifdef __cplusplus
}
#endif



#endif /* EOF */
