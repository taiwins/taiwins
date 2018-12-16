#ifndef TW_UI_H
#define TW_UI_H

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdbool.h>


//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <sequential.h>

#include "../config.h"
#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////Application style definition/////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////


struct tw_rgba_t {
	uint8_t r,g,b,a;
};

struct widget_colors {
	struct tw_rgba_t normal;
	struct tw_rgba_t hover;
	struct tw_rgba_t active;
};

/* the simpler version of style config, it is not gonna be as fancy as
 * nuklear(supporting nk_style_time(image or color) for all gui elements), but
 * it will have a consistent look
 */
struct taiwins_theme {
	uint32_t row_size; //this defines the text size as well
	struct tw_rgba_t window_color;
	struct tw_rgba_t border_color;
	//text colors
	struct tw_rgba_t text_color; //text_edit_cursor as well
	struct tw_rgba_t text_active_color;
	//widget color
	struct widget_colors button;
	struct widget_colors toggle;
	struct widget_colors select;
	struct tw_rgba_t slider_bg_color;
	struct widget_colors slider;
	struct widget_colors chart;
	struct tw_rgba_t combo_color;

	//you only need to set one font right now, use it for normal text render, icons is done by system
	char font[256];
	//there could other fonts, but hope not so many
};

extern const struct taiwins_theme taiwins_dark_theme;
extern const struct taiwins_theme taiwins_light_theme;


/**
 * this function exams the theme(color and fonts are valid and do some convert)
 */
bool tw_validate_theme(struct taiwins_theme *);

/* return -1 if path is not long enough, even if you feed a empty string we should return a font */
int tw_find_font_path(const char *font_name, char *path, size_t path_size);



static inline int
tw_font_pt2px(int pt_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (ppi / 72.0 * pt_size);
}

static inline int
tw_font_px2pt(int px_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (72.0 * px_size / ppi);
}



unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);

//////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////geometry definition////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
struct point2d {
	unsigned int x;
	unsigned int y;
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

//////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////APPSURFACE/////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

enum APP_SURFACE_TYPE {
	APP_BACKGROUND,
	APP_PANEL,
	APP_WIDGET,
	APP_LOCKER,
};

enum taiwins_btn_t {
	TWBTN_LEFT,
	TWBTN_RIGHT,
	TWBTN_MID,
	TWBTN_DCLICK,
};

struct wl_globals;
struct app_surface;
struct egl_env;

typedef void (*keycb_t)(struct app_surface *, xkb_keysym_t, uint32_t, int);
typedef void (*pointron_t)(struct app_surface *, uint32_t, uint32_t);
typedef void (*pointrbtn_t)(struct app_surface *, enum taiwins_btn_t, bool, uint32_t, uint32_t);
typedef void (*pointraxis_t)(struct app_surface *, int, int, uint32_t, uint32_t);
/* This actually implements the wl_callback callback. */
typedef void (*frame_t)(struct app_surface *, uint32_t user_data);

/**
 * /brief Templated wl_surface container
 *
 * The design goal of the surface is for user to have a few frame-update
 * routines, so the user will never need to call the `wl_surface_damage`,
 * `wl_surface_attach` and `wl_surface_commit`.
 *
 * An sample routine for wl_buffers? We need to provide a wl_buffer to draw,
 * then it uses that buffer to commit. For that special case, it needs two draw
 * calls, one does the attach->damage->commit, the other one does that
 * framebuffer manipulation.
 *
 * In the nuklear's case, you need one callback for manipulating the surface and
 * the other one has the control over nuklear context.
 *
 * And there could be other cases, so here we just have a callback for the
 * swap-chain, the specific draw is implement else where.
 */
struct app_surface {
	//the structure to store wl_shell_surface, xdg_shell_surface or tw_ui
	struct wl_proxy *protocol;
	//geometry information
	unsigned int px, py; //anchor
	unsigned int w, h; //size
	unsigned int s;
	enum APP_SURFACE_TYPE type;

	struct wl_globals *wl_globals;
	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	bool need_animation;
	/* buffer */
	union {
		struct {
			struct shm_pool *pool;
			struct wl_buffer  *wl_buffer[2];
			bool dirty[2];
			bool committed[2];
		};
		struct {
			/* we do not necessary need it but when it comes to
			 * surface destroy you will need it */
			EGLDisplay egldisplay;
			struct wl_egl_window *eglwin;
			EGLSurface eglsurface;
		};
		/**
		 * the parent pointer, means you will have to call the
		 * parent->frame function to do the work
		 */
		struct app_surface *parent;
		/* if we have an vulkan surface, it would be the same thing */
	};
	/* callbacks */
	struct {
		keycb_t keycb;
		pointron_t pointron;
		pointrbtn_t pointrbtn;
		pointraxis_t pointraxis;
		frame_t do_frame;
	};
	//destructor
	void (*destroy)(struct app_surface *);
	//XXX this user_data is controlled by implementation, do not set it!!!
	void *user_data;
};

/** TODO
 * new APIs for the appsurface
 */

/**
 * /brief clean start a new appsurface
 */
void app_surface_init(struct app_surface *surf, struct wl_surface *, struct wl_proxy *proxy);

/**
 * /brief the universal release function
 */
static inline void
app_surface_release(struct app_surface *surf)
{
	if (surf->destroy)
		surf->destroy(surf);
	//throw all the callbacks
	surf->keycb = NULL;
	surf->pointron = NULL;
	surf->pointrbtn = NULL;
	surf->pointraxis = NULL;
	wl_surface_destroy(surf->wl_surface);
	if (surf->protocol)
		wl_proxy_destroy(surf->protocol);
}

/**
 * /brief kick off the frames of the app_surface
 *
 * user call this function to start drawing. It triggers the frames untils
 * app_surface_release is called.
 */
static inline void
app_surface_frame(struct app_surface *surf, bool anime)
{
	//this is the best we
	surf->need_animation = anime;
	if (anime) {
		surf->keycb = NULL;
		surf->pointron = NULL;
		surf->pointrbtn = NULL;
		surf->pointraxis = NULL;
	}
	surf->do_frame(surf, 0);
}

/* request a frame to make run it later */
void app_surface_request_frame(struct app_surface *surf);


/**
 * /brief a helper function if you have egl_env
 *
 * I do not want to include this but there is a fixed way to allocate
 * `wl_egl_window` and `EGLSurface`, even with nuklear
 */
void app_surface_init_egl(struct app_surface *surf, struct egl_env *env);

cairo_format_t
translate_wl_shm_format(enum wl_shm_format format);

size_t
stride_of_wl_shm_format(enum wl_shm_format format);


static inline struct app_surface *
app_surface_from_wl_surface(struct wl_surface *s)
{
	return (struct app_surface *)wl_surface_get_user_data(s);
}


/**
 * /brief one of the implementation of app_surface
 *
 * In this case, the user_data is taken and used for callback, so we do not need
 * to allocate new memory, since you won't have extra space for ther other
 * user_data, expecting to embend app_surface in another data structure.
 *
 */
typedef void (*shm_buffer_draw_t)(struct app_surface *surf, struct wl_buffer *buffer,
				  int32_t *dx, int32_t *dy, int32_t *dw, int32_t *dh);

void
shm_buffer_impl_app_surface(struct app_surface *surf, struct shm_pool *pool,
			    shm_buffer_draw_t draw_call, uint32_t w, uint32_t h);


/**
 * /brief second implementation we provide here is the parent surface
 */
void embeded_impl_app_surface(struct app_surface *surf, struct app_surface *parent,
			      uint32_t w, uint32_t h, uint32_t px, uint32_t py);


#ifdef __cplusplus
}
#endif



#endif /* EOF */
