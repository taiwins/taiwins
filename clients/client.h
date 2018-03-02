#ifndef TWCLIENT_H
#define TWCLIENT_H

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <GL/gl.h>
//hopefully this shit is declared
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <sequential.h>
#include <buffer.h>

#include "egl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
struct tw_event {
	void *data;
	int (*cb)(void *);
};


struct tw_event_queue {
	queue_t event_queue;
};

extern struct tw_event_queue event_queue
*/


/**
 * struct for one application, it should normally contains those
 */
struct wl_globals {
	struct wl_shm *shm;
	enum wl_shm_format buffer_format;
	struct wl_compositor *compositor;
	struct wl_display *display;
	struct egl_env eglenv;
	struct wl_inputs {
		struct wl_seat *wl_seat;
		struct wl_keyboard *wl_keyboard;
		struct wl_pointer *wl_pointer;
		struct wl_touch *wl_touch;
		char name[64];
		//xkb informations
		struct xkb_context *kcontext;
		struct xkb_keymap  *keymap;
		struct xkb_state   *kstate;
		//cursor information
		char cursor_theme_name[64];
		struct wl_cursor *cursor;
		struct wl_cursor_theme *cursor_theme;
		struct wl_surface *cursor_surface;
		struct wl_buffer *cursor_buffer;
		struct wl_surface *focused_surface;
		uint32_t cursor_events;
		uint32_t cx, cy;
		uint32_t axis;
		bool axis_pos;
	} inputs;
};

//void app_surface_init(struct wl_compositor *p)


/* here you need a data structure that glues the anonoymous_buff and wl_buffer wl_shm_pool */
int wl_globals_announce(struct wl_globals *globals,
			struct wl_registry *wl_registry,
			uint32_t name,
			const char *interface,
			uint32_t version);

void wl_globals_init(struct wl_globals *globals, struct wl_display *display);
void wl_globals_release(struct wl_globals *globals);


/******************************************************************************
 *
 * a wl_buffer managerment solution, using a pool based approach
 *
 *****************************************************************************/
struct shm_pool {
	struct anonymous_buff_t file;
	struct wl_shm *shm;
	struct wl_shm_pool *pool;
	list_t wl_buffers;
};

int shm_pool_create(struct shm_pool *pool, struct wl_shm *shm, int size);

/** allocated a peice of buffer
 *
 * it could be a previously used or new one, but once the buffer is created, it
 * will not be really destroyed unless shm_pool_destroy is called.
 */
struct wl_buffer *shm_pool_alloc_buffer(struct shm_pool *pool, size_t width, size_t height);

/** declare this buffer is not in use anymore, so when we need a new one we can
 * reuse it
 */
void shm_pool_buffer_release(struct wl_buffer *wl_buffer);

/** access or activate the buffer, if the buffer is previously released, we will
 * activate it again
 *
 * use this to avoid shm_pool_alloc_buffer process if you wish to manage the
 * buffers yourself
 */
void *shm_pool_buffer_access(struct wl_buffer *wl_buffer);

size_t shm_pool_buffer_size(struct wl_buffer *wl_buffer);

void shm_pool_destroy(struct shm_pool *pool);

#ifdef __cplusplus
}
#endif



#endif /* EOF */
