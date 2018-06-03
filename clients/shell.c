#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cairo/cairo.h>

#include <sys/inotify.h>
#include <poll.h>
//damn it, you need to poll that thing as well

#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include "client.h"
#include "shellui.h"
#include "egl.h"

struct taiwins_shell *shelloftaiwins;


//here we define the one queue
struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	struct egl_env eglenv;
	//right now we only have one output, but we still keep the info
	list_t outputs;
	//the event queue
	struct tw_event_queue client_event_queue;
	bool quit;
} oneshell; //singleton

struct tw_event_queue *the_event_processor = &oneshell.client_event_queue;

struct shell_output {
	struct desktop_shell *shell;
	struct wl_output *output;
	struct app_surface background;
	struct shell_panel panel;
	struct shm_pool pool;

	list_t link;
	bool inited;
};



/******************************************************************************/
/**************************** panneaux fonctions ******************************/
/******************************************************************************/
static void sample_wiget(struct nk_context *ctx, float width, float height)
{
	//TODO, change the draw function to app->draw_widget(app);
	enum {EASY, HARD};
	static int op = EASY;
	static int property = 20;
	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);
	//I can try to use the other textures
	/* if (nk_button_label(ctx, "button")) { */
	/*	fprintf(stderr, "button pressed\n"); */
	/* } */
	nk_layout_row_dynamic(ctx, 30, 2);
	if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
	if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;
}


static void
_launch_widget(struct shell_widget *widget)
{
	//find the widget launch point
	struct point2d point;
	int x = widget->icon.box.x + widget->icon.box.w/2;
	/* int y = app->icon.box.y; */
	int ww = widget->width;
	/* int wh = app->height; */
	struct shell_panel *panel = widget->panel;
	unsigned int pw = panel->panelsurf.w;
	/* unsigned int ph = panel->panelsurf.h; */
	if (x + ww/2 >= pw) {
		point.x = pw - ww;
	} else if (x - ww/2 < 0) {
		point.x = 0;
	} else
		point.x = x - ww/2;
	point.y = 10;

	shell_panel_show_widget(widget->panel, point.x, point.y);
	nk_egl_launch(widget->panel->backend, widget->width, widget->height, 1.0, sample_wiget,
		      NULL, 0);
}

static void
shell_panel_click(struct app_surface *surf, bool btn, uint32_t cx, uint32_t cy)
{
	struct shell_panel *panel = container_of(surf, struct shell_panel, panelsurf);
	fprintf(stderr, "clicked on button (%d, %d)\n", cx, cy);
	struct shell_widget *w = shell_panel_find_widget_at_xy(panel, cx, cy);
	if (w)
		_launch_widget(w);
}

static void
shell_panel_init(struct shell_panel *panel, struct shell_output *w)
{
	struct app_surface *s = &panel->panelsurf;
	appsurface_init(s, NULL, APP_PANEL, w->shell->globals.compositor, w->output);
	appsurface_init_input(s, NULL, NULL, shell_panel_click, NULL);
	appsurface_init(&panel->widget_surface, NULL, APP_WIDGET,
			w->shell->globals.compositor, w->output);
	panel->backend = nk_egl_create_backend(&w->shell->eglenv, panel->widget_surface.wl_surface);
	panel->widgets = (vector_t){0};

}

static void
shell_panel_destroy(struct shell_panel *panel)
{
	struct app_surface *s = &panel->panelsurf;
	appsurface_destroy(s);
	nk_egl_destroy_backend(panel->backend);
	appsurface_destroy(&panel->widget_surface);
	vector_destroy(&panel->widgets);
}

void
shell_panel_show_widget(struct shell_panel *panel, int x, int y)
{
	struct shell_output *w = container_of(panel, struct shell_output, panel);
	struct app_surface *surf = &panel->widget_surface;
	taiwins_shell_set_widget(w->shell->shell, surf->wl_surface, w->output, x, y);
}
void shell_panel_hide_widget(struct shell_panel *panel)
{
	struct shell_output *w = container_of(panel, struct shell_output, panel);
	struct app_surface *surf = &panel->widget_surface;
	taiwins_shell_hide_widget(w->shell->shell, surf->wl_surface);
}

/******************************************************************************/
/************************** arriere-plan fonctions ****************************/
/******************************************************************************/
static void
shell_background_load(struct app_surface *background)
{
	if (background->committed[1])
		return;
	void *buffer1 = shm_pool_buffer_access(background->wl_buffer[1]);
	printf("background surface wl_buffer %p, buffer: %p\n", background->wl_buffer[1],
	       buffer1);
	char imgpath[100];
	sprintf(imgpath, "%s/.wallpaper/wallpaper.png", getenv("HOME"));
	if (load_image(imgpath, WL_SHM_FORMAT_ARGB8888, background->w, background->h,
		       (unsigned char *)buffer1) != buffer1) {
		fprintf(stderr, "failed to load image somehow\n");
	}
	background->dirty[1] = true;
}

static void
output_init(struct shell_output *w)
{
	struct taiwins_shell *shell = w->shell->shell;
	shm_pool_create(&w->pool, w->shell->globals.shm, 4096, w->shell->globals.buffer_format);
	//arriere-plan
	appsurface_init(&w->background, NULL, APP_BACKGROUND, w->shell->globals.compositor, w->output);
	taiwins_shell_set_background(shell, w->output, w->background.wl_surface);
	//panel
	shell_panel_init(&w->panel, w);
	taiwins_shell_set_panel(shell, w->output, w->panel.panelsurf.wl_surface);
	w->inited = true;
}

static void
output_create(struct shell_output *w, struct wl_output *wl_output, struct desktop_shell *twshell)
{
	*w = (struct shell_output){0};
	w->shell = twshell;
	//we don't have the buffer format here
	w->inited = false;
	w->output = wl_output;
	wl_output_set_user_data(wl_output, w);
	if (w->shell->shell)
		output_init(w);
}


static void
output_distroy(struct shell_output *o)
{
	wl_output_release(o->output);
	appsurface_destroy(&o->background);
	shell_panel_destroy(&o->panel);
	shm_pool_destroy(&o->pool);
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
	struct shell_output *output;
	//damn it, we need a hack
	struct app_surface *appsurf = (struct app_surface *)wl_surface_get_user_data(surface);

	if (appsurf->type == APP_BACKGROUND)
		output = container_of(appsurf, struct shell_output, background);
	else if (appsurf->type == APP_PANEL) {
		struct shell_panel *p = container_of(appsurf, struct shell_panel, panelsurf);
		output = container_of(p, struct shell_output, panel);
	}

	int32_t w = scale *(width - edges);
	int32_t h = scale *(height - edges);

	struct bbox bounding = {
		.w = scale * (width - edges),
		.h = scale * (height - edges),
		.x = 0,
		.y = 0,
	};
	appsurface_init_buffer(appsurf, &output->pool, &bounding);
	void *buffer0 = shm_pool_buffer_access(appsurf->wl_buffer[0]);
	void *buffer1 = shm_pool_buffer_access(appsurf->wl_buffer[1]);

	if (appsurf->type == APP_BACKGROUND) {
		shell_background_load(appsurf);
		appsurface_fadc(appsurf);

	} else if (appsurf->type == APP_PANEL) {
		memset(buffer0, 255, w*h*4);
		memset(buffer1, 255, w*h*4);

		appsurf->dirty[1] = true;
		appsurface_fadc(appsurf);
		struct shell_panel *p = container_of(appsurf, struct shell_panel, panelsurf);
		//now create a new widget
		struct shell_widget *app = shell_panel_add_widget(p);
		shell_widget_init_with_funcs(app, calendar_icon, NULL);
		struct tw_event update_icon = {.data = app,
					       .cb = update_icon_event,
		};
		tw_event_queue_add_source(the_event_processor, NULL, 1000, &update_icon, IN_MODIFY);
		/* struct eglapp *another; */
		/* another = eglapp_addtolist(&output->panel); */
		/* eglapp_init_with_funcs(another, calendar_icon, NULL); */
		/* update_icon.data = another; */
		/* tw_event_producer_add_source(the_event_producer, NULL, 1000, &update_icon, IN_MODIFY); */

	}

}

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
		shelloftaiwins = twshell->shell;
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct shell_output *output = malloc(sizeof(*output));
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
	wl_globals_init(&shell->globals, display);
	list_init(&shell->outputs);
	shell->shell = NULL;
	shell->quit = false;
	egl_env_init(&shell->eglenv, display);
	tw_event_queue_start(&shell->client_event_queue, display);
}



static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);
	struct shell_output *w, *next;
	list_for_each_safe(w, next, &shell->outputs, link) {
		list_remove(&w->link);
		output_distroy(w);
		free(w);
	}
	wl_globals_release(&shell->globals);
	egl_env_end(&shell->eglenv);
	shell->quit = true;
	shell->client_event_queue.quit = true;
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}




int
main(int argc, char **argv)
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
		struct shell_output *w, *next;
		list_for_each_safe(w, next, &oneshell.outputs, link) {
			if (!w->inited)
				output_init(w);
		}
	}
	while(wl_display_dispatch(display) != -1);
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
//	desktop_shell_release(oneshell);
	return 0;
}
