#include <unistd.h>
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

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	struct app_surface background;
	//TODO make panel a single app_surface, and move widgets into shell
	struct shell_panel panel;
	struct shm_pool pool;
};

/* we would want this structure to work with shell protocol, not just desktop
 * protocol, so rename it would be necessary
 */
struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	struct egl_env eglenv;

	struct shell_output shell_outputs[16];

	struct wl_list shell_widgets;
	struct tw_event_queue client_event_queue;
	//states
	vector_t uinit_outputs;
	bool quit;
} oneshell; //singleton

struct tw_event_queue *the_event_processor = &oneshell.client_event_queue;





/******************************************************************************/
/**************************** panneaux fonctions ******************************/
/******************************************************************************/

static void sample_wiget(struct nk_context *ctx, float width, float height, void *data)
{
	//TODO, change the draw function to app->draw_widget(app);
	enum {EASY, HARD};
	static int op = EASY;
	static struct nk_text_edit text_edit;
	static bool init_text_edit = false;
	static char text_buffer[256];
	if (!init_text_edit) {
		init_text_edit = true;
		nk_textedit_init_fixed(&text_edit, text_buffer, 256);
	}
	const char *a = "aaaa";
//	char *text_buffer = nk_egl_access_text_buffer(ctx, &textlen, &tmp);

	nk_layout_row_static(ctx, 30, 80, 2);
	nk_button_label(ctx, "button");
	nk_label(ctx, "another", NK_TEXT_LEFT);

	nk_layout_row_dynamic(ctx, 30, 2);
	if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
	if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;

	nk_layout_row_dynamic(ctx, 25, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &text_edit, nk_filter_default);
	if (nk_egl_get_keyinput(ctx) == XKB_KEY_Tab)
		nk_textedit_text(&text_edit, a, 4);
	else if (nk_egl_get_keyinput(ctx) == XKB_KEY_Print)
		nk_egl_capture_framebuffer(ctx, "/tmp/capture.png");
	//we need a for loop to do the undo...
	//okay, I can try to meddle with textedit cursor but I guess it will
	//fucked up the text_edit.

	//to support autocomplete, I will have to use `nk_textedit_text` to
	//insert the text. then if we need to circulate the context, we need to
	//undo the text then insert the new one
}

static void
_launch_widget(struct shell_widget *widget, struct app_surface *widget_surface)
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
	nk_egl_launch(widget->panel->backend, widget_surface,
		      sample_wiget, NULL);
}

static void
shell_panel_click(struct app_surface *surf, enum taiwins_btn_t btn, bool state, uint32_t cx, uint32_t cy)
{
	struct shell_panel *panel = container_of(surf, struct shell_panel, panelsurf);
	struct app_surface *widget_surface = &panel->widget_surface;
	fprintf(stderr, "clicked on button (%d, %d)\n", cx, cy);
	struct shell_widget *w = shell_panel_find_widget_at_xy(panel, cx, cy);
	widget_surface->w =  w->width;
	widget_surface->h = w->height;
	widget_surface->s = 1.0;
	if (w && state)
		_launch_widget(w, widget_surface);
}

static void
shell_panel_init(struct shell_panel *panel, struct shell_output *w)
{
	struct app_surface *s = &panel->panelsurf;
	appsurface_init(s, NULL, APP_PANEL, w->shell->globals.compositor, w->output);
	appsurface_init_input(s, NULL, NULL, shell_panel_click, NULL);
	appsurface_init(&panel->widget_surface, NULL, APP_WIDGET,
			w->shell->globals.compositor, w->output);
	panel->backend = nk_egl_create_backend(&w->shell->eglenv);
	panel->widgets = (vector_t){0};

}

static void
shell_panel_destroy(struct shell_panel *panel)
{
	struct app_surface *s = &panel->panelsurf;
	appsurface_release(s);
	nk_egl_destroy_backend(panel->backend);
	appsurface_release(&panel->widget_surface);
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
initialize_shell_output(struct shell_output *w, struct tw_output *tw_output,
			struct desktop_shell *shell)
{
	w->shell = shell;
	w->output = tw_output;
	shm_pool_init(&w->pool, shell->globals.shm, 4096, shell->globals.buffer_format);

	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *bg_ui =
		taiwins_shell_create_background(shell->shell, bg_sf, tw_output);
	appsurface_init1(&w->background, NULL, APP_BACKGROUND, bg_sf,
			 (struct wl_proxy *)bg_ui);

	struct wl_surface *pn_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *pn_ui =
		taiwins_shell_create_panel(shell->shell, pn_sf, tw_output);
	appsurface_init1(&w->panel.panelsurf, NULL, APP_PANEL, pn_sf,
			 (struct wl_proxy *)pn_ui);

	tw_output_set_user_data(tw_output, w);
}

static void
release_shell_output(struct shell_output *w)
{
	struct app_surface *surfaces[] = {
		&w->panel.panelsurf,
		&w->background,
	};
	for (int i = 0; i < 2; i++)
		appsurface_release(surfaces[i]);

	//you may destroy the buffer before released
	shm_pool_release(&w->pool);
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
	struct desktop_shell *shell =
		taiwins_shell_get_user_data(taiwins_shell);
	struct shell_output *output;
	//damn it, we need a hack
	struct app_surface *appsurf = (struct app_surface *)wl_surface_get_user_data(surface);

	if (appsurf->type == APP_BACKGROUND)
		output = container_of(appsurf, struct shell_output, background);
	else if (appsurf->type == APP_PANEL) {
		struct shell_panel *p = container_of(appsurf, struct shell_panel, panelsurf);
		output = container_of(p, struct shell_output, panel);
	} else
		return;

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
		fprintf(stderr, "we have panel!\n");
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
		struct timespec interval = {
			.tv_sec = 1,
			.tv_nsec = 0,
		};
		shell->client_event_queue.quit =
			!tw_event_queue_add_timer(the_event_processor, &interval, &update_icon);
//		tw_event_queue_add_timer(the_event_processor, &interval, &update_icon);
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



/************************** desktop_shell_interface ********************************/

static inline bool
desktop_shell_ready(struct desktop_shell *shell)
{
	return shell->shell && is_shm_format_valid(shell->globals.buffer_format);
}

/* just know this code has side effect: it works even you removed and plug back
 * the output, since n_outputs returns at the first time it hits NULL. Even if
 * there is output afterwards, it won't know, so next time when you plug in
 * another monitor, it will choose the emtpy slots.
 */
static inline int
desktop_shell_n_outputs(struct desktop_shell *shell)
{
	for (int i = 0; i < 16; i++)
		if (shell->shell_outputs[i].output == NULL)
			return i;
	return 16;
}

static inline void
desktop_shell_refill_outputs(struct desktop_shell *shell)
{
	for (int i = 0, j=0; i < 16 && j <= i; i++) {
		//check i
		if (shell->shell_outputs[j].output == NULL)
			continue;
		//check j
		if (i==j)
			j++;
		else
			shell->shell_outputs[j++] = shell->shell_outputs[i];
	}
}

static inline
desktop_shell_ith_output(struct desktop_shell *shell, struct tw_output *output)
{
	for (int i = 0; i < 16; i++)
		if (shell->shell_outputs[i].output == output)
			return i;
	return -1;
}

static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	wl_globals_init(&shell->globals, display);
	shell->shell = NULL;
	shell->quit = false;
	egl_env_init(&shell->eglenv, display);
	tw_event_queue_init(&shell->client_event_queue);
	shell->client_event_queue.quit =
		!tw_event_queue_add_wl_display(&shell->client_event_queue, display);
	vector_init(&shell->uinit_outputs, sizeof(struct tw_output *), NULL);
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);
	struct shell_output *w, *next;
	for (int i = 0; i < desktop_shell_n_outputs(shell); i++)
		release_shell_output(&shell->shell_outputs[i]);
	wl_globals_release(&shell->globals);
	egl_env_end(&shell->eglenv);
	shell->quit = true;
	shell->client_event_queue.quit = true;
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}

static void
desktop_shell_try_add_tw_output(struct desktop_shell *shell, struct tw_output *tw_output)
{
	if (desktop_shell_ready(shell)) {
		int n = desktop_shell_n_outputs(shell);
		initialize_shell_output(&shell->shell_outputs[n], tw_output, shell);
	} else
		vector_append(&shell->uinit_outputs, &tw_output);
}
static void
desktop_shell_init_rest_outputs(struct desktop_shell *shell)
{
	for (int n = desktop_shell_n_outputs(shell), j = 0;
	     j < shell->uinit_outputs.len; j++) {
		struct shell_output *w = &shell->shell_outputs[n+j];
		initialize_shell_output(w, vector_at(&shell->uinit_outputs, j),
					shell);
	}
}
/************************** desktop_shell_interface ********************************/
/*********************************** end *******************************************/


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
	} else if (!strcmp(interface, tw_output_interface.name)) {
		struct tw_output *tw_output =
			wl_registry_bind(wl_registry, name, &tw_output_interface, version);
		desktop_shell_try_add_tw_output(twshell, tw_output);
	/* } else if (!strcmp(interface, wl_output_interface.name)) { */
	/*	struct shell_output *output = malloc(sizeof(*output)); */
	/*	struct wl_output *wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, version); */
	/*	output_create(output, wl_output, twshell); */
	/*	list_append(&twshell->outputs, &output->link); */
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



int
main(int argc, char **argv)
{
	//shell-taiwins size is 112 it is not that
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
	desktop_shell_init_rest_outputs(&oneshell);

	wl_display_flush(display);
	tw_event_queue_run(&oneshell.client_event_queue);
//	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
//	desktop_shell_release(oneshell);
	return 0;
}
