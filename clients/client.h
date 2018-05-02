#ifndef TWCLIENT_H
#define TWCLIENT_H

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <poll.h>
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

#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

//this is accessible API
struct tw_event {
	void *data;
	int (*cb)(void *);
};

//client side event processor
struct tw_event_queue {
	struct wl_display *wl_display;
	pthread_t thread;
	long timeout;
	int inotify_fd;
	struct pollfd pollfd;
	list_t head;
	bool quit;
};

extern struct tw_event_queue *the_event_processor;
void *tw_event_queue_run(void *event_queue);
bool tw_event_queue_start(struct tw_event_queue *queue, struct wl_display *display);
/** add an file based event if file is provided, or time based event if timeout is provided
 */
bool tw_event_queue_add_source(struct tw_event_queue *queue, const char *file, long timeout,
			       struct tw_event *event, uint32_t mask);

//we need also the modifier enum
enum modifier_mask {
	TW_NOMOD = 0,
	TW_ALT = 1,
	TW_CTRL = 2,
	TW_SUPER = 4,
	TW_SHIFT = 8,
};


/**
 * struct for one application, it should normally contains those
 */
struct wl_globals {
	struct wl_shm *shm;
	enum wl_shm_format buffer_format;
	struct wl_compositor *compositor;
	struct wl_display *display;
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
		uint32_t cx, cy; //current coordinate of the cursor
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
	enum wl_shm_format format;
};

//we should also add format
int shm_pool_create(struct shm_pool *pool, struct wl_shm *shm, int size, enum wl_shm_format format);

/** allocated a peice of buffer
 *
 * it could be a previously used or new one, but once the buffer is created, it
 * will not be really destroyed unless shm_pool_destroy is called.
 */
struct wl_buffer *shm_pool_alloc_buffer(struct shm_pool *pool, size_t width, size_t height);

/** declare this buffer is not in use anymore, so when we need a new one we can
 * reuse it
 */
void shm_pool_buffer_free(struct wl_buffer *wl_buffer);

void shm_pool_wl_buffer_set_release(struct wl_buffer *wl_buffer,
				    void (*cb)(void *, struct wl_buffer *),
				    void *data);

void *shm_pool_buffer_access(struct wl_buffer *wl_buffer);

size_t shm_pool_buffer_size(struct wl_buffer *wl_buffer);

void shm_pool_destroy(struct shm_pool *pool);

#ifdef __cplusplus
}
#endif



#endif /* EOF */
