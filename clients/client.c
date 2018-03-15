#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <poll.h>

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <sequential.h>
#include <buffer.h>

#include "client.h"
#include "ui.h"


////////////////////////////wayland listeners///////////////////////////


static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	if (globals->buffer_format == WL_SHM_FORMAT_ARGB8888 ||
		globals->buffer_format == WL_SHM_FORMAT_RGB888 ||
		globals->buffer_format == WL_SHM_FORMAT_RGBA8888)
		return;
	//it maynot be a good idea, it is just that we are given a choice
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
		globals->buffer_format = format;
		break;
	case WL_SHM_FORMAT_RGB888:
		globals->buffer_format = format;
		break;
	case WL_SHM_FORMAT_RGBA8888:
		globals->buffer_format = format;
		break;
	default:
		fprintf(stderr, "I don't know this format%X\n", format);
		break;
	}
}

static struct wl_shm_listener shm_listener = {
	shm_format
};


static uint32_t
kc_linux2xkb(uint32_t kc_linux)
{
	//this should only work on x11, but very weird it works all the time
	return kc_linux+8;
}



static void
handle_key(void *data,
	   struct wl_keyboard *wl_keyboard,
	   uint32_t serial,
	   uint32_t time,
	   uint32_t key,
	   uint32_t state)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	xkb_keycode_t keycode = kc_linux2xkb(key);
	xkb_keysym_t  keysym  = xkb_state_key_get_one_sym(globals->inputs.kstate,
							  keycode);
	//every surface it self is an app_surface, in thise case
	struct wl_surface *focused = globals->inputs.focused_surface;
	struct app_surface *appsurf = app_surface_from_wl_surface(focused);
	if (appsurf->keycb)
		appsurf->keycb(appsurf, keysym);
//	fprintf(stderr, "key keyprinting didn't work\n");
	//and we know if this surface is app_surface, no, you couldn't assume that right.
}


static void
handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
	      uint32_t format,
	      int32_t fd,
	      uint32_t size)
{
	struct wl_globals *globals = (struct wl_globals *)data;

	if (globals->inputs.kcontext)
		xkb_context_unref(globals->inputs.kcontext);
	void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	globals->inputs.kcontext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
//	printf("%s\n", addr);
	globals->inputs.keymap = xkb_keymap_new_from_string(globals->inputs.kcontext,
							    (const char *)addr,
							    XKB_KEYMAP_FORMAT_TEXT_V1,
							    XKB_KEYMAP_COMPILE_NO_FLAGS);
	globals->inputs.kstate = xkb_state_new(globals->inputs.keymap);
	munmap(addr, size);
}

static
void handle_modifiers(void *data,
		      struct wl_keyboard *wl_keyboard,
		      uint32_t serial,
		      uint32_t mods_depressed, //which key
		      uint32_t mods_latched,
		      uint32_t mods_locked,
		      uint32_t group)
{
	struct wl_globals *globals = (struct wl_globals *)data;
//	fprintf(stderr, "We pressed a modifier\n");
	//I guess this serial number is different for each event
	//wayland uses layout group. you need to know what xkb_matched_layout is
	xkb_state_update_mask(globals->inputs.kstate,
			      mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

//you must have this
static void
handle_repeat_info(void *data,
			    struct wl_keyboard *wl_keyboard,
			    int32_t rate,
			    int32_t delay)
{

}

static void
handle_keyboard_enter(void *data,
			   struct wl_keyboard *wl_keyboard,
			   uint32_t serial,
			   struct wl_surface *surface,
			   struct wl_array *keys)
{
	//this job is done by pointer
//	fprintf(stderr, "keyboard got focus\n");
}
static void
handle_keyboard_leave(void *data,
		    struct wl_keyboard *wl_keyboard,
		    uint32_t serial,
		    struct wl_surface *surface)
{
//	fprintf(stderr, "keyboard lost focus\n");
}


static
struct wl_keyboard_listener keyboard_listener = {
	.key = handle_key,
	.modifiers = handle_modifiers,
	.enter = handle_keyboard_enter,
	.leave = handle_keyboard_leave,
	.keymap = handle_keymap,
	.repeat_info = handle_repeat_info,

};

//////////////////////////////////////////////////////////////////////////
/////////////////////////////Pointer listeners////////////////////////////
//////////////////////////////////////////////////////////////////////////

//this code is used to accumlate the a sequence of event for the cursor, they
//are mutually exclusive and have priorities
enum POINTER_EVENT_CODE {
	//the event doesn't need to be handle
	POINTER_ENTER = 0,
	POINTER_LEAVE = 0,
	//motion
	POINTER_MOTION = 1,
	//the btn group, no middle key handled
	POINTER_BTN_LEFT =  1,
	POINTER_BTN_RIGHT = 2,
	POINTER_BTN = 4,
	//axis events, we only handle a few.
	POINTER_AXIS = 8
};



static void
pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	static bool cursor_set = false;
	struct wl_globals *globals = (struct wl_globals *)data;
	globals->inputs.focused_surface = surface;
	if (!cursor_set) {
		struct wl_surface *csurface = globals->inputs.cursor_surface;
		struct wl_buffer *cbuffer = globals->inputs.cursor_buffer;
		wl_pointer_set_cursor(wl_pointer, serial, csurface, 16, 16);
		wl_surface_attach(csurface, cbuffer, 0, 0);
		wl_surface_damage(csurface, 0, 0, 32, 32);
		wl_surface_commit(csurface);
	}

	globals->inputs.cursor_events = POINTER_ENTER;
}

static void
pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	globals->inputs.focused_surface = NULL;

	globals->inputs.cursor_events = POINTER_LEAVE;
}


static void
pointer_motion(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t serial,
	       wl_fixed_t surface_x,
	       wl_fixed_t surface_y)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	globals->inputs.cx = wl_fixed_to_int(surface_x);
	globals->inputs.cy = wl_fixed_to_int(surface_y);
//	fprintf(stderr, "the mostion is (%d, %d)\n", surface_x/256, surface_x/256);

	globals->inputs.cursor_events |= POINTER_MOTION;
}

//frame function call the callbacks
static void
pointer_frame(void *data,
	      struct wl_pointer *wl_pointer)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	if (!globals->inputs.cursor_events)
		return;
	//with the line above, we won't have any null surface problem
	struct wl_surface *focused = globals->inputs.focused_surface;
	struct app_surface *appsurf = app_surface_from_wl_surface(focused);
	uint32_t event = globals->inputs.cursor_events;
	//events goes in the order
	if ((event & POINTER_AXIS) && appsurf->pointraxis)
		appsurf->pointraxis(appsurf,
				    globals->inputs.axis_pos, globals->inputs.axis,
				    globals->inputs.cx, globals->inputs.cy);
	else if ((event & POINTER_BTN) && appsurf->pointrbtn)
		appsurf->pointrbtn(appsurf, event & POINTER_BTN_LEFT,
				   globals->inputs.cx, globals->inputs.cy); //well, you never know
	else if ((event & POINTER_MOTION) && appsurf->pointron)
		appsurf->pointron(appsurf, globals->inputs.cx, globals->inputs.cy);

	//finally erase all previous events
	globals->inputs.cursor_events = 0;
}


static void
pointer_button(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t serial,
	       uint32_t time,
	       uint32_t button,
	       uint32_t state)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	if (!state) { //only register events at the end of the pointer
		switch (button) {
		case BTN_LEFT:
			globals->inputs.cursor_events |= POINTER_BTN;
			globals->inputs.cursor_events |= POINTER_BTN_LEFT;
			break;
		case BTN_RIGHT:
			globals->inputs.cursor_events |= POINTER_BTN;
			globals->inputs.cursor_events |= POINTER_BTN_RIGHT;
			break;
		default:
			break;
		}
	}
}

static void
pointer_axis(void *data,
	     struct wl_pointer *wl_pointer,
	     uint32_t time,
	     uint32_t axis,
	     wl_fixed_t value)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	//in the surface coordinates, (0, 0) sits on top left, so we need to
	//reverse it
	globals->inputs.axis_pos = wl_fixed_to_int(value);
	globals->inputs.axis = axis;

	globals->inputs.cursor_events |= POINTER_AXIS;
}

static void
pointer_axis_src(void *data,
		 struct wl_pointer *wl_pointer, uint32_t src)
{
//	fprintf(stderr, "axis src event\n");
}

static void
pointer_axis_stop(void *data,
		  struct wl_pointer *wl_pointer,
		  uint32_t time, uint32_t axis) {
//	fprintf(stderr, "axis end event\n");
}

static void
pointer_axis_discret(void *data, struct wl_pointer *wl_pointer,
		     uint32_t axis, int32_t discrete)
{
	//useless for a mouse
//	fprintf(stderr, "axis discrete event\n");
}

//make all of the them available, so we don't crash
static struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.frame = pointer_frame,
	.button = pointer_button,
	.axis  = pointer_axis,
	.axis_source = pointer_axis_src,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discret,
};


static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	strncpy(globals->inputs.name, name,
		min(numof(globals->inputs.name), strlen(name)));
	fprintf(stderr, "we have this seat with a name called %s\n", name);
}



static void
seat_capabilities(void *data,
		       struct wl_seat *wl_seat,
		       uint32_t capabilities)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		fprintf(stderr, "got a keyboard\n");
		globals->inputs.wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(globals->inputs.wl_keyboard, &keyboard_listener, globals);
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		globals->inputs.wl_pointer = wl_seat_get_pointer(wl_seat);
		fprintf(stderr, "got a mouse\n");
		//TODO use the name in the global
		globals->inputs.cursor_theme = wl_cursor_theme_load("Vanilla-DMZ", 32, globals->shm);
		globals->inputs.cursor = wl_cursor_theme_get_cursor(globals->inputs.cursor_theme, "plus");
		globals->inputs.cursor_surface = wl_compositor_create_surface(globals->compositor);
		globals->inputs.cursor_buffer = wl_cursor_image_get_buffer(globals->inputs.cursor->images[0]);
//		wl_pointer_set_user_data(globals->inputs.wl_pointer, globals);
		wl_pointer_add_listener(globals->inputs.wl_pointer, &pointer_listener, globals);
	}
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		globals->inputs.wl_touch = wl_seat_get_touch(wl_seat);
		fprintf(stderr, "got a touchpad\n");
	}

}


static struct wl_seat_listener seat_listener = {

	.capabilities = seat_capabilities,
	.name = seat_name,
};

static bool COMMITTED = false;
static void
app_surface_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
	COMMITTED = false;
//	struct app_surface *appsurf = (struct app_surface *)data;
	if (wl_callback)
		wl_callback_destroy(wl_callback);
}


static struct wl_callback_listener app_surface_done_listener = {
	.done = app_surface_done
};

void
appsurface_swap_dbuffer(struct app_surface *surf)
{
	struct wl_buffer *tmp = surf->wl_buffer[0];
	bool tmpcommit  = surf->committed[0];
	bool tmpdirty = surf->dirty[0];

	surf->wl_buffer[0] = surf->wl_buffer[1];
	surf->committed[0] = surf->committed[1];
	surf->dirty[0] = surf->dirty[1];

	surf->wl_buffer[1] = tmp;
	surf->committed[1] = tmpcommit;
	surf->dirty[1] = tmpdirty;
}

//it must have at least one buffer libre.
void
appsurface_fadc(struct app_surface *surf)
{
	//if b2 is not free, we shouldn't do anything.
	if(surf->committed[1] || !surf->dirty[1]) {
		return;
	}
	wl_surface_attach(surf->wl_surface, surf->wl_buffer[1], 0, 0);
	struct wl_callback *callback = wl_surface_frame(surf->wl_surface);
	wl_callback_add_listener(callback, &app_surface_done_listener, surf);
	wl_surface_damage(surf->wl_surface, 900, surf->py, 100, surf->h);
	wl_surface_commit(surf->wl_surface);
	surf->committed[1] = true;
	//this way we should guarentee that all the committed surface is clean now.
	surf->dirty[1] = false;
	//if b1 is not free, then we have no change issues.
	appsurface_swap_dbuffer(surf);
	COMMITTED = true;
//	if (!surf->committed[0])
//		appsurface_swap_dbuffer(surf);

}

void appsurface_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	fprintf(stderr, "buffer %p  released.\n", wl_buffer);
	struct app_surface *appsurf = (struct app_surface *)data;

	if (wl_buffer == appsurf->wl_buffer[0]) {
		if (COMMITTED)
//			fprintf(stderr, "we have framebuffer released before done\n");
		appsurf->committed[0] = false;
		if (appsurf->committed[1])
			appsurface_swap_dbuffer(appsurf);
	}
	else if (wl_buffer == appsurf->wl_buffer[1]) {
		appsurf->committed[1] = false;
//		appsurf->dirty[1] = false;
	}
	//other cases:
	//0) b1 free and b2 free. You don't need to do anything
	//1) b2 free and b1 not. nothing to do
	//2) b2 free and b1 free. nothing to do
}


//wl_globals functions

void
wl_globals_init(struct wl_globals *globals, struct wl_display *display)
{
	//do this first, so all the pointers are null
	*globals = (struct wl_globals){0};
	globals->display = display;
	globals->buffer_format = WL_SHM_FORMAT_XRGB2101010;

}


void wl_globals_release(struct wl_globals *globals)
{
	if (globals->inputs.wl_pointer) {
		wl_pointer_destroy(globals->inputs.wl_pointer);
	}
	if (globals->inputs.cursor_theme) {
		//there is no need to destroy the cursor wl_buffer or wl_cursor,
		//it gets cleaned up automatically in theme_destroy
		wl_cursor_theme_destroy(globals->inputs.cursor_theme);
		wl_surface_destroy(globals->inputs.cursor_surface);
		globals->inputs.cursor_theme = NULL;
		globals->inputs.cursor = NULL;
		globals->inputs.cursor_buffer = NULL;
		globals->inputs.cursor_surface = NULL;
		globals->inputs.focused_surface = NULL;
	}
}



int wl_globals_announce(struct wl_globals *globals,
			struct wl_registry *wl_registry,
			uint32_t name,
			const char *interface,
			uint32_t version)
{
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		globals->inputs.wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(globals->inputs.wl_seat, &seat_listener, globals);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		globals->compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)  {
		globals->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
		wl_shm_add_listener(globals->shm, &shm_listener, globals);
	} else {
		fprintf(stderr, "announcing global %s\n", interface);
		return 0;
	}
	return 1;
}



/* void */
/* tw_event_queue_init(struct tw_event_queue *q) */
/* { */
/*	queue_init(&q->event_queue, sizeof(struct tw_event), NULL); */
/*	pthread_mutex_init(&q->mutex, NULL); */
/*	q->quit = false; */
/* } */
/* void */
/* tw_event_queue_destroy(struct tw_event_queue *q) */
/* { */
/*	q->quit = true; */
/*	queue_destroy(&q->event_queue); */
/* } */

/* void */
/* tw_event_queue_append_event(struct tw_event_queue ppp*q, void *data, int (*cb)(void *)) */
/* { */
/*	struct tw_event event = {.data = data, .cb=cb}; */
/*	pthread_mutex_lock(&q->mutex); */
/*	queue_append(&q->event_queue, &event); */
/*	pthread_mutex_unlock(&q->mutex); */
/* } */

/* static inline bool */
/* tw_event_queue_empty(struct tw_event_queue *q) */
/* { */
/*	bool empty = false; */
/*	pthread_mutex_lock(&q->mutex); */
/*	empty = queue_empty(&q->event_queue); */
/*	pthread_mutex_unlock(&q->mutex); */
/*	return empty; */
/* } */


/* void */
/* tw_event_queue_dispatch(struct tw_event_queue *q) */
/* { */
/*	while (!tw_event_queue_empty(q)) { */

/*		pthread_mutex_lock(&q->mutex); */
/*		struct tw_event event = *(struct tw_event *)queue_top(&q->event_queue); */
/*		queue_pop(&q->event_queue); */
/*		pthread_mutex_unlock(&q->mutex); */

/*		event.cb(event.data); */
/*	} */
/* } */

//this should not
struct tw_event_source {
	int wd;
	struct tw_event event;
	list_t node;
	//for time-based event
	long duration;
	long progress;
};

static int
process_events(struct tw_event_queue *queue)
{
	char *ptr;
	size_t len;
	char buff[4096];
	struct tw_event_source *es, *next;
	const struct inotify_event *event;
	//I think I can read as much I want
	len = read(queue->inotify_fd, buff, sizeof(buff));
	if (len <= 0)
		return -1;
	for (ptr = buff; ptr < buff+len;
	     ptr += sizeof(struct inotify_event) + event->len) {
		event = (const struct inotify_event *)ptr;
		list_for_each_safe(es, next, &queue->head, node) {
			if (es->wd == event->wd) {
				//run the event
				es->event.cb(es->event.data);
				break;
			}
		}
	}
	return 0;
}

static int
process_timeout_event(struct tw_event_queue *queue)
{
	int processed = 0;
	struct tw_event_source *es, *next;
	list_for_each_safe(es, next, &queue->head, node) {
		if (es->wd == -1 && es->progress + queue->timeout >= es->duration) {
			es->progress += queue->timeout - es->duration;
			es->event.cb(es->event.data);
			processed = 1;
		} else {
			es->progress += queue->timeout;
			processed = 0;
		}
	}
	return processed;
}

//the thread function
void *
tw_event_queue_run(void *event_queue)
{
	struct tw_event_queue *queue = (struct tw_event_queue *)event_queue;
	//poll->produce-event-or-timeout
	while (!queue->quit) {
		//you will need a queue since you can have
		//1) more than one event happen at the same time
		//2) events get lost if they can't be processed or queued at the same time.
		int poll_num = poll(&queue->pollfd, 1, 1000);
		if (poll_num > 0 && (queue->pollfd.events & POLLIN)) {
			process_events(queue);
		} else if (poll_num == 0)
			process_timeout_event(queue);
		wl_display_flush(queue->wl_display);
	}
	//now it does the clean up, so we dont need a destructor
	struct tw_event_source *es, *next;

	close(queue->inotify_fd);
	list_for_each_safe(es, next, &queue->head, node)
		free(es);
	return NULL;
}

bool
tw_event_queue_start(struct tw_event_queue *queue, struct wl_display *display)
{
	queue->wl_display = display;
	list_init(&queue->head);
	int fd = inotify_init1(IN_NONBLOCK);
	if (fd == -1)
		return false;
	queue->quit = false;
	queue->inotify_fd = fd;
	queue->pollfd.fd = fd;
	queue->pollfd.events = POLLIN;
	queue->timeout = 60000; //initially set to one minute
	pthread_create(&queue->thread, NULL, tw_event_queue_run, queue);
	return true;
}


bool
tw_event_queue_add_source(struct tw_event_queue *queue, const char *file, long timeout,
			  struct tw_event *event, uint32_t mask)
{
	int fd;
	if (file) {
		fd = inotify_add_watch(queue->inotify_fd, file, mask);
		if (fd == -1)
			return false;
		timeout = queue->timeout + 1;
	} else
		fd = -1;
	struct tw_event_source *event_source =
		(struct tw_event_source *)malloc(sizeof(struct tw_event_source));
	queue->timeout = min(queue->timeout, timeout);
	event_source->event = *event;
	event_source->wd = fd;
	event_source->duration = timeout;
	event_source->progress = 0;
	list_append(&queue->head, &event_source->node);
	//TODO call the event for the first time
	event->cb(event->data);
	return true;
}
