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



struct app_style {
	int32_t background;
	int32_t foreground;
};



struct app_surface {
	/**
	 * the parent pointer. It is only in the sub_surfaces, so the parent has
	 * no information about where the subclasses are. TODO you need to
	 * change the subclasses into list and expose the bbox information
	*/
	struct app_surface *parent;
	/**
	 * app tree management
	 */
	struct app_surface * (*find_subapp_at_xy)(int x, int y);
	int (*n_subapp)(void);
	/* the callback is for sub-struct calling */
	int (*paint_subsurface)(struct app_surface *surf, const struct bbox *, const void *,
				 enum wl_shm_format);
	struct app_surface * (*add_subapp)(struct app_surface *surf);
	//geometry information
	unsigned int px, py; //anchor
	unsigned int w, h; //size
	enum APP_SURFACE_TYPE type;
	//buffer management
	struct shm_pool *pool;
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	struct wl_buffer  *wl_buffer[2];
	bool dirty[2];
	bool committed[2];

	//input management
	void (*keycb)(struct app_surface *surf, xkb_keysym_t keysym);
	//run this function at the frame callback
	void (*pointron)(struct app_surface *surf, uint32_t sx, uint32_t sy);
	//left is true, right is false
	void (*pointrbtn)(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy);
	//axis events with direction (0->x, y->1)
	void (*pointraxis)(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy);
	//things to do when input occuped by other surfaces
	void (*defocused)(struct app_surface *surf);
};

/**
 * /brief initialize the appsurface to a default state
 *
 * no input, no subapp, no buffer, but has a wl_surface
 */
void appsurface_init(struct app_surface *surf, struct app_surface *parent,
		     enum APP_SURFACE_TYPE type, struct wl_compositor *compositor,
		     struct wl_output *output);
void appsurface_destroy(struct app_surface *surf);
/**
 * /brief assign wl_buffers to the appsurface, thus it initialize the double
 * buffer state and commit state
 *
 */
void appsurface_init_buffer(struct app_surface *surf, struct shm_pool *shm,
			    const struct bbox *bbox);
/**
 * /brief assign all the callbacks for subapps, if not used, the function will
 * stay unavailable
 */
void appsurface_initfor_subapps(struct app_surface *surf,
				struct app_surface *(*find_subapp)(int, int),
				int (*n_subapp)(void),
				int (*paint)(struct app_surface *,
					     const struct bbox *, const void *,
					     enum wl_shm_format),
				struct app_surface *(*add)(struct app_surface *));
/**
 * /brief init all the input callbacks, zero is acceptable
 */
void appsurface_init_input(struct app_surface *surf,
			   void (*keycb)(struct app_surface *surf, xkb_keysym_t keysym),
			   void (*pointron)(struct app_surface *surf, uint32_t sx, uint32_t sy),
			   void (*pointrbtn)(struct app_surface *surf, bool btn, uint32_t sx, uint32_t sy),
			   void (*pointraxis)(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy));

void appsurface_assign_shouldquit(struct app_surface *surf,
				  void (*quit)(struct app_surface *));

void appsurface_fadc(struct app_surface *surf);

void appsurface_buffer_release(void *data, struct wl_buffer *wl_buffer);


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
