#ifndef TW_UI_H
#define TW_UI_H


#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <sequential.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);

enum APP_SURFACE_TYPE {
	APP_BACKGROUND,
	APP_PANEL,
	APP_WIDGET,
	APP_LOCKER,
};

struct bbox {
	unsigned int x;
	unsigned int y;
	unsigned int w;
	unsigned int h;
};

static inline bool
bbox_contain_point(const struct bbox *box, unsigned int x, unsigned int y)
{
	return ((x >= box->x) &&
		(x < box->x + box->w) &&
		(y >= box->y) &&
		(y < box->y + box->h));
}

static inline bool
bboxs_intersect(const struct bbox *ba, const struct bbox *bb)
{
	return (ba->x < bb->x+bb->w) && (ba->x+ba->w > bb->x) &&
		(ba->y < bb->y+bb->h) && (ba->y + ba->h > bb->y);
}


//this is the root structure by all the surfaces in the shell, others should
//extend on it. And wl_surface's usr data should point to it
struct app_surface {
	//you will always need this
	unsigned int px, py; //anchor
	unsigned int w, h; //size

	enum APP_SURFACE_TYPE type;
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	struct wl_buffer  *wl_buffer;
	//callbacks for wl_pointer and cursor
	void (*keycb)(struct app_surface *surf, xkb_keysym_t keysym);
	//run this function at the frame callback
	void (*pointron)(struct app_surface *surf, uint32_t sx, uint32_t sy);
	//left is true, right is false
	void (*pointrbtn)(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy);
	//axis events with direction (0->x, y->1)
	void (*pointraxis)(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy);
};

cairo_format_t
translate_wl_shm_format(enum wl_shm_format format);


static inline struct app_surface *
app_surface_from_wl_surface(struct wl_surface *s)
{
	return (struct app_surface *)wl_surface_get_user_data(s);
}

#ifdef __cplusplus
}
#endif



#endif /* EOF */
