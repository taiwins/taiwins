#ifndef TW_IMAGE_H
#define TW_IMAGE_H


//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif


unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);

#ifdef __cplusplus
}
#endif



#endif /* EOF */
