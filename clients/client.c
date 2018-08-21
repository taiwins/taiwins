#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
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
#include <os/buffer.h>

#include "client.h"
#include "ui.h"


////////////////////////////wayland listeners///////////////////////////

//okay, this is when we chose the best format
static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	//the priority of the format ARGB8888 > RGBA8888 > RGB888
	struct wl_globals *globals = (struct wl_globals *)data;
	//we have to use ARGB because cairo uses this format
	if (format == WL_SHM_FORMAT_ARGB8888) {
		globals->buffer_format = WL_SHM_FORMAT_ARGB8888;
//		fprintf(stderr, "we choosed argb8888 for the application\n");
	}
	else if (format == WL_SHM_FORMAT_RGBA8888 &&
		 globals->buffer_format != WL_SHM_FORMAT_ARGB8888) {
		globals->buffer_format = WL_SHM_FORMAT_RGBA8888;
//		fprintf(stderr, "we choosed rgba8888 for the application\n");
	}
	else if (format == WL_SHM_FORMAT_RGB888 &&
		 globals->buffer_format != WL_SHM_FORMAT_ARGB8888 &&
		 globals->buffer_format != WL_SHM_FORMAT_RGBA8888) {
		globals->buffer_format = WL_SHM_FORMAT_RGB888;
//		fprintf(stderr, "we choosed rgb888 for the application\n");
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


static uint32_t
modifier_mask_from_xkb_state(struct xkb_state *state)
{
	uint32_t mask = TW_NOMOD;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_SUPER;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_SHIFT;
	return mask;
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
	uint32_t modifier = modifier_mask_from_xkb_state(globals->inputs.kstate);
	/* char keyname[100]; */
	/* xkb_keysym_get_name(keysym, keyname, 100); */
	/* fprintf(stderr, "the key pressed is %s\n", keyname); */
	//every surface it self is an app_surface, in thise case
	struct wl_surface *focused = globals->inputs.focused_surface;
	if (!focused)
		return;
	struct app_surface *appsurf = app_surface_from_wl_surface(focused);
	//I suppose the modifier key is called as well
	if (appsurf->keycb)
		appsurf->keycb(appsurf, keysym, modifier,
			       (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? 1 : 0);
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
	//every surface it self is an app_surface, in thise case
//	struct wl_surface *focused = globals->inputs.focused_surface;
//	struct app_surface *appsurf = app_surface_from_wl_surface(focused);
//	if (appsurf->modcb)
//		appsurf->modcb(appsurf, );
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
	globals->inputs.keymap = xkb_keymap_new_from_string(globals->inputs.kcontext,
							    (const char *)addr,
							    XKB_KEYMAP_FORMAT_TEXT_V1,
							    XKB_KEYMAP_COMPILE_NO_FLAGS);
	globals->inputs.kstate = xkb_state_new(globals->inputs.keymap);
	munmap(addr, size);
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
	struct wl_globals *globals = (struct wl_globals *)data;
	globals->inputs.focused_surface = surface;
	//this job is done by pointer
	fprintf(stderr, "keyboard got focus\n");
	/* struct wl_surface *focused = globals->inputs.focused_surface; */
	/* struct app_surface *appsurf = app_surface_from_wl_surface(focused); */
	/* //I suppose the modifier key is called as well */
	/* if (appsurf->keycb) */
	/*	appsurf->keycb(appsurf, XKB_KEY_NoSymbol, TW_NOMOD, 0); */
}
static void
handle_keyboard_leave(void *data,
		    struct wl_keyboard *wl_keyboard,
		    uint32_t serial,
		    struct wl_surface *surface)
{
	struct wl_globals *globals = (struct wl_globals *)data;
	globals->inputs.focused_surface = NULL;
	fprintf(stderr, "keyboard lost focus\n");
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
pointer_cursor_done(void *data, struct wl_callback *callback, uint32_t callback_data)
{
	/* fprintf(stderr, "cursor set!\n"); */
	wl_callback_destroy(callback);
}


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
		struct wl_callback *callback = wl_surface_frame(csurface);
		globals->inputs.cursor_done_listener.done = pointer_cursor_done;
		wl_callback_add_listener(callback, &globals->inputs.cursor_done_listener, NULL);
		//give a role to the the cursor_surface
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
	if (!focused)
		return;
	struct app_surface *appsurf = app_surface_from_wl_surface(focused);

	uint32_t event = globals->inputs.cursor_events;
	//events goes in the order, but why I am using else if?
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
		globals->inputs.cursor_theme = wl_cursor_theme_load("whiteglass", 32, globals->shm);
		globals->inputs.cursor = wl_cursor_theme_get_cursor(globals->inputs.cursor_theme, "plus");
		globals->inputs.cursor_surface = wl_compositor_create_surface(globals->compositor);
		globals->inputs.cursor_buffer = wl_cursor_image_get_buffer(globals->inputs.cursor->images[0]);
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



//wl_globals functions

void
wl_globals_init(struct wl_globals *globals, struct wl_display *display)
{
	//do this first, so all the pointers are null
	*globals = (struct wl_globals){0};
	globals->display = display;
	globals->buffer_format = 0xFFFFFFFF;
}


void
wl_globals_release(struct wl_globals *globals)
{
	//destroy the input
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
	if (globals->inputs.wl_pointer) {
		wl_pointer_destroy(globals->inputs.wl_pointer);
	}
	if (globals->inputs.wl_keyboard)
		wl_keyboard_destroy(globals->inputs.wl_keyboard);
	wl_seat_destroy(globals->inputs.wl_seat);
	wl_shm_destroy(globals->shm);
	wl_compositor_destroy(globals->compositor);
}



int
wl_globals_announce(struct wl_globals *globals,
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


void
wl_globals_bg_color_rgb(const struct wl_globals *globals, float *r, float *g, float *b)
{
	//because we are on the little edian system, the order is actually bgra
	union color_encoder {
		int32_t code;
		char bgra[4];
	} parser;

	parser.code = globals->style.background;
	*r = parser.bgra[2] / 256.0;
	*g = parser.bgra[1] / 256.0;
	*b = parser.bgra[0] / 256.0;
}

void
wl_globals_fg_color_rgb(const struct wl_globals *globals, float *r, float *g, float *b)
{
	union color_encoder {
		int32_t code;
		char bgra[4];
	} parser;

	parser.code = globals->style.foreground;
	*r = parser.bgra[2] / 256.0;
	*g = parser.bgra[1] / 256.0;
	*b = parser.bgra[0] / 256.0;
}


//event queue implementation
struct tw_event_source {
	struct epoll_event poll_event;
	struct tw_event event;
};


void
tw_event_queue_run(struct tw_event_queue *queue)
{
	struct epoll_event events[32];
	struct tw_event_source *event_source;
	struct tw_event *event;
	//poll->produce-event-or-timeout
	while (!queue->quit) {
		int count = epoll_wait(queue->pollfd, events, 32, 0);
		for (int i = 0; i < count; i++) {
			event = events[i].data.ptr;
			event->cb(event->data);
		}
	}
	for (int i = 0; i < queue->sources.len; i++) {
		event_source = (struct tw_event_source *)vector_at(&queue->sources, i);
		event = &event_source->event;
		epoll_ctl(queue->pollfd, EPOLL_CTL_DEL,
			  event_source->poll_event.data.fd, NULL);
		if (event->free)
			event->free(event->data);
	}
	close(queue->pollfd);
	return;
}

bool
tw_event_queue_init(struct tw_event_queue *queue)
{
	int fd = epoll_create1(EPOLL_CLOEXEC);
	if (fd == -1)
		return false;

	vector_init(&queue->sources, sizeof(struct epoll_event), NULL);
	queue->pollfd = fd;
	queue->quit = false;
	return true;
}



bool
tw_event_queue_add_source(struct tw_event_queue *queue, int fd,
			  struct tw_event *e, uint32_t mask)
{
	//luckly we have newelem method.
	struct tw_event_source *event_source = vector_newelem(&queue->sources);
	event_source->event = *e;
	event_source->poll_event.data.fd = fd;
	event_source->poll_event.data.ptr = &event_source->event;
	event_source->poll_event.events = mask;

	if (epoll_ctl(queue->pollfd, EPOLL_CTL_ADD, fd, &event_source->poll_event))
		return false;

	e->cb(e->data);
	return true;
}


bool
tw_event_queue_add_timer(struct tw_event_queue *queue, const struct timespec *interval,
			 struct tw_event *e)
{
	int fd;
	struct itimerspec spec = {
		.it_interval = *interval,
		.it_value = {0},
	};
	struct tw_event_source *event_source;
	fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (!fd)
		return false;
	timerfd_settime(fd, 0, &spec, NULL);
	event_source = vector_newelem(&queue->sources);
	event_source->event = *e;
	event_source->poll_event.data.fd = fd;
	event_source->poll_event.data.ptr = &event_source->event;
	event_source->poll_event.events = EPOLLIN | EPOLLOUT;

	if (epoll_ctl(queue->pollfd, EPOLL_CTL_ADD, fd, &event_source->poll_event)) {
		close(fd);
		return false;
	}
	e->cb(e->data);
	return true;
}

int dispatch_wl_event(void *data)
{
	struct wl_display *display = data;
	wl_display_prepare_read(display);
	wl_display_read_events(display);
	wl_display_dispatch_pending(display);
}

bool
tw_event_queue_add_wl_display(struct tw_event_queue *queue, struct wl_display *display)
{
	int fd = wl_display_get_fd(display);
	struct tw_event dispatch_display = {
		.data = display,
		.cb = dispatch_wl_event,
	};
	struct tw_event_source *event_source;
	event_source = vector_newelem(&queue->sources);
	event_source->event = dispatch_display;
	event_source->poll_event.data.fd = fd;
	event_source->poll_event.data.ptr = &event_source->event;
	event_source->poll_event.events = EPOLLIN;

	epoll_ctl(queue->pollfd, EPOLL_CTL_ADD, fd, &event_source->poll_event);
}
