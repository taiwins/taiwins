#ifndef TW_UI_H
#define TW_UI_H


//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif


unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);


struct app_surface {
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	struct wl_buffer  *wl_buffer;
	//callbacks for wl_pointer and cursor
	void (*keycb)(struct app_surface *surf, unsigned int key);

};





#ifdef __cplusplus
}
#endif



#endif /* EOF */
