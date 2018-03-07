#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <sys/inotify.h>
#include <poll.h>
//damn it, you need to poll that thing as well

#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include "client.h"
#include "shellui.h"
#include "egl.h"


//here we define the one queue
struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	struct egl_env eglenv;
	//right now we only have one output, but we still keep the info
	list_t outputs;
	//the event queue
	struct tw_event_queue event_queue;
	struct tw_event_producer event_producer;
	bool quit;
} oneshell; //singleton

struct tw_event_queue *the_event_queue = &oneshell.event_queue;
struct tw_event_producer *the_event_producer = &oneshell.event_producer;

struct output_widgets {
	struct desktop_shell *shell;
	struct wl_output *output;
	struct app_surface background;
	struct shell_panel panel;
	struct shm_pool pool;

	list_t link;
	bool inited;
};



static void
panel_click_on_icon(struct app_surface *surf, bool btn, uint32_t cx, uint32_t cy)
{
	struct shell_panel *panel = container_of(surf, struct shell_panel, panelsurf);
	fprintf(stderr, "clicked on button (%d, %d)\n", cx, cy);
	for (int i = 0; i < panel->widgets.len; i++) {
		struct eglapp *app = (struct eglapp *)vector_at(&panel->widgets, i);
		struct eglapp_icon *icon = icon_from_eglapp(app);
		struct app_surface *appsurf = appsurface_from_icon(icon);
		if (bbox_contain_point(&icon->box, cx, cy) && appsurf->pointrbtn) {

			eglapp_launch(app, &oneshell.eglenv, oneshell.globals.compositor);
			break;
		}
		//TODO else, we should close application if we can, same as background
	}
}

static void
shell_panel_update(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
//	fprintf(stderr, "we should have update here\n");
	//the callback_data is time
	struct shell_panel *panel = (struct shell_panel *)data;
	/* wl_surface_attach(panel->panelsurf.wl_surface, panel->panelsurf.wl_buffer, 0, 0); */
	/* wl_surface_damage_buffer(panel->panelsurf.wl_surface, */
	/*			 panel->dirty_area.x, panel->dirty_area.y, */
	/*			 panel->dirty_area.w, panel->dirty_area.h); */
	/* wl_surface_commit(panel->panelsurf.wl_surface); */
	//the callback is done, we need to distroy it
	wl_callback_destroy(wl_callback);
}

static void
shell_panel_init(struct shell_panel *panel, struct output_widgets *w)
{
	struct app_surface *s = &panel->panelsurf;
	panel->panelsurf = (struct app_surface){0};
	s->wl_surface = wl_compositor_create_surface(w->shell->globals.compositor);
	s->keycb = NULL;
	s->pointron = NULL;
	s->pointraxis = NULL;
	s->pointrbtn = panel_click_on_icon;
	wl_surface_set_user_data(s->wl_surface, s);
	s->wl_output = w->output;
	s->type = APP_PANEL;
	panel->widgets = (vector_t){0};
	//TODO DO change this...
	panel->format = WL_SHM_FORMAT_ARGB8888;
	panel->update_cb.done = shell_panel_update;

}

static void
shell_background_init(struct app_surface *background, struct output_widgets *w)
{
	w->background = (struct app_surface){0};
	background->wl_surface = wl_compositor_create_surface(w->shell->globals.compositor);
	background->wl_output = w->output;
//	background->keycb = NULL;
	wl_surface_set_user_data(background->wl_surface, background);
	background->type = APP_BACKGROUND;
}

static void
output_init(struct output_widgets *w)
{
	struct taiwins_shell *shell = w->shell->shell;
	//arriere-plan
	shell_background_init(&w->background, w);
	taiwins_shell_set_background(shell, w->output, w->background.wl_surface);
	//panel
	shell_panel_init(&w->panel, w);
	taiwins_shell_set_panel(shell, w->output, w->panel.panelsurf.wl_surface);
	w->inited = true;
}

static void
output_create(struct output_widgets *w, struct wl_output *wl_output, struct desktop_shell *twshell)
{
	w->shell = twshell;
	shm_pool_create(&w->pool, twshell->globals.shm, 4096);
	w->inited = false;
	w->output = wl_output;
	wl_output_set_user_data(wl_output, w);
	if (w->shell->shell)
		output_init(w);
}


static void
output_distroy(struct output_widgets *o)
{
	wl_output_release(o->output);
}





static void
shell_configure_surface(void *data,
			struct taiwins_shell *taiwins_shell,
			struct wl_surface *surface,
			uint32_t scale,
			uint32_t edges,
			int32_t width,
			int32_t height)
{
	struct output_widgets *output;
	//damn it, we need a hack
	struct app_surface *appsurf = (struct app_surface *)wl_surface_get_user_data(surface);

	if (appsurf->type == APP_BACKGROUND)
		output = container_of(appsurf, struct output_widgets, background);
	else if (appsurf->type == APP_PANEL)
		output = container_of(appsurf, struct output_widgets, panel);

	int32_t w = scale *(width - edges);
	int32_t h = scale *(height - edges);
	appsurf->pool = &output->pool;

	void *buffer_addr = NULL;
	struct wl_buffer *new_buffer = shm_pool_alloc_buffer(&output->pool, w, h);
	buffer_addr = shm_pool_buffer_access(new_buffer);
	appsurf->w = w;
	appsurf->h = h;
	appsurf->px = 0;
	appsurf->py = 0;

	if (appsurf->type == APP_BACKGROUND) {
		printf("background surface buffer %p, wl_buffer: %p\n", buffer_addr, new_buffer);
		char imgpath[100];
		sprintf(imgpath, "%s/.wallpaper/wallpaper.png", getenv("HOME"));
		if (load_image(imgpath, WL_SHM_FORMAT_ARGB8888, w, h,
			       (unsigned char *)buffer_addr) != buffer_addr) {
			fprintf(stderr, "failed to load image somehow\n");
		}
		wl_surface_attach(output->background.wl_surface, new_buffer, 0, 0);
		wl_surface_damage(output->background.wl_surface, 0, 0, w, h);
		wl_surface_commit(output->background.wl_surface);
		//TODO maybe using the double buffer?
//		if (output->background.wl_buffer)
//			shm_pool_buffer_release(output->background.wl_buffer);
		output->background.wl_buffer = new_buffer;

	} else if (appsurf->type == APP_PANEL) {
//		struct shell_panel *panel = container_of(appsurf, struct shell_panel, panelsurf);

		printf("panel surface buffer %p, wl_buffer: %p\n", buffer_addr, new_buffer);
		memset(buffer_addr, 255, w*h*4);
		wl_surface_attach(output->panel.panelsurf.wl_surface, new_buffer, 0, 0);
		wl_surface_damage(output->panel.panelsurf.wl_surface, 0, 0, w, h);
		wl_surface_commit(output->panel.panelsurf.wl_surface);
		if (output->panel.panelsurf.wl_buffer)
			shm_pool_buffer_release(output->panel.panelsurf.wl_buffer);
		output->panel.panelsurf.wl_buffer = new_buffer;

		struct eglapp *app;
		app = eglapp_addtolist(&output->panel);
		eglapp_init_with_funcs(app, calendar_icon, NULL);
		struct tw_event update_icon = {.data = app,
					       .cb = update_icon_event,
		};
		tw_event_producer_add_source(the_event_producer, NULL, 1000, &update_icon, IN_MODIFY);
		struct eglapp *another;
		another = eglapp_addtolist(&output->panel);
		eglapp_init_with_funcs(another, calendar_icon, NULL);
		update_icon.data = another;
		tw_event_producer_add_source(the_event_producer, NULL, 1000, &update_icon, IN_MODIFY);

	}

}
/* if (output->background.wl_buffer[0]) */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/* swap(output->background.wl_buffer[0], */
/*      output->background.wl_buffer[1]); */
/* if (!output->background.wl_buffer[0]) */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* else if (shm_pool_buffer_size(output->background.wl_buffer[0]) != w * h * 4) { */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* } */
/* buffer_addr = shm_pool_buffer_access(output->background.wl_buffer[0]); */



static struct taiwins_shell_listener taiwins_listener = {
	.configure = shell_configure_surface
};


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_shell *twshell = (struct desktop_shell *)data;

	if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		twshell->shell = (struct taiwins_shell *)
			wl_registry_bind(wl_registry, name, &taiwins_shell_interface, version);
		taiwins_shell_add_listener(twshell->shell, &taiwins_listener, twshell);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct output_widgets *output = malloc(sizeof(*output));
		struct wl_output *wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, version);
		output_create(output, wl_output, twshell);
		list_append(&twshell->outputs, &output->link);

	} else
		wl_globals_announce(&twshell->globals, wl_registry, name, interface, version);
}

static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{

}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};




//options.

//1) make it static, adding all the inotify entry all at once before we can use
//it.  if we decide to go this way, when to add the thread? Obviously, it it
//after all the registeration of the widgets , but it should also before the
//widgets because if not, you will need the inotify when you run the widgets.

//so it has to be dynamiclly. The the widgets well registre all the inotify
//entry, but it will cause a aabb lock problem

static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	tw_event_queue_init(the_event_queue);
	wl_globals_init(&shell->globals, display);
	list_init(&shell->outputs);
	shell->shell = NULL;
	shell->quit = false;
//	egl_env_init(&shell->eglenv, display);
	tw_event_producer_start(&shell->event_producer);
}



static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);
	struct output_widgets *w, *next;
	list_for_each_safe(w, next, &shell->outputs, link) {
		list_remove(&w->link);
		output_distroy(w);
	}
	wl_globals_release(&shell->globals);
	tw_event_queue_destroy(the_event_queue);
//	egl_env_end(&shell->eglenv);
	shell->quit = true;
}





int main(int argc, char **argv)
{
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "couldn't connect to wayland display\n");
		return -1;
	}
	desktop_shell_init(&oneshell, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &oneshell);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	{	//initialize the output
		struct output_widgets *w, *next;
		list_for_each_safe(w, next, &oneshell.outputs, link) {
			if (!w->inited)
				output_init(w);
		}
	}

	//also, you need to create some kind of event_queue for the shell, and
	//launch a thread to read this event queue, a event should have a
	//listener, call the listener
	while(wl_display_dispatch(display) != -1)
		tw_event_queue_dispatch(the_event_queue);
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
//	desktop_shell_release(oneshell);
	return 0;
}
