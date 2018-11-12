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
#include "../config.h"
#include "client.h"
#include "egl.h"
#include "nk_wl_egl.h"
#include "widget.h"

struct taiwins_shell *shelloftaiwins;

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	//for right now we just need to draw the icons from left to right. Later
	//we may have better solution
	struct app_surface background;
	struct app_surface panel;

	struct nk_egl_backend *panel_backend;
	struct shm_pool pool;
};

struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	struct egl_env eglenv;

	struct shell_output shell_outputs[16];

	struct nk_egl_backend *widget_backend;
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

static void
shell_panel_frame(struct nk_context *ctx, float width, float height, void *data)
{
	enum nk_buttons button;
	uint32_t sx, sy;
	struct nk_vec2 launch_point;
	struct shell_output *shell_output = data;
	struct desktop_shell *shell = shell_output->shell;
	struct app_surface *panel = &shell_output->panel;

	//a temporary hack
	static int no_widget = 0;
	//actual drawing
	int i = 0;
	size_t n_widgets =  wl_list_length(&shell->shell_widgets);
	struct shell_widget *widget = NULL, *clicked = NULL;
	nk_layout_row_begin(ctx, NK_STATIC, panel->h - 12, n_widgets);
	wl_list_for_each(widget, &shell->shell_widgets, link) {
		enum nk_buttons btn;
		uint32_t sx, sy;
		widget->ancre_cb(ctx, width, height, widget);
		/* if (nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT) || ) */
		if (nk_egl_get_btn(ctx, &btn, &sx, &sy))
			clicked = widget;
		i++;
	}
	nk_layout_row_end(ctx);
	if (!clicked)
		return;
	//TODO remove this two lines
	no_widget += 1;
	no_widget = no_widget % n_widgets;

	//we should essentially use nuklear builtin functions
	nk_egl_get_btn(ctx, &button, &sx, &sy);
	//determine the launch point
	if (sx + clicked->widget.w/2 >= panel->w)
		launch_point.x = panel->w - clicked->widget.w;
	else if (sx - clicked->widget.w/2 < 0)
		launch_point.x = 0;
	else
		launch_point.x = sx - clicked->widget.w/2;
	launch_point.y = sy;
	//en preference, we do this after the panel commit

	//the widget is already there or the widget does not need to draw, we
	//just return
	if (clicked->widget.protocol || !clicked->draw_cb)
		return;
	//change this part!!!
	struct wl_surface *widget_surface = wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *widget_proxy = taiwins_shell_launch_widget(shell->shell, widget_surface,
								 shell_output->output,
								 launch_point.x, launch_point.y);

	appsurface_init(&clicked->widget, NULL, APP_WIDGET, widget_surface,
			 (struct wl_proxy *)widget_proxy);
	clicked->widget.w = 400;
	clicked->widget.h = 400;
	clicked->widget.s = 1;

	appsurface_init_egl(&clicked->widget, &shell->eglenv);

	nk_egl_launch(shell->widget_backend, &clicked->widget, clicked->draw_cb, clicked);
}

static void
tw_background_configure(void *data,
			struct tw_ui *tw_ui,
			uint32_t width,
			uint32_t height,
			uint32_t scale)
{
	struct shell_output *w = data;
	struct app_surface *background = &w->background;

	struct desktop_shell *shell = w->shell;

	background->w = width;
	background->h = height;
	background->s = scale;
	shm_pool_init(&w->pool, shell->globals.shm, 4096, shell->globals.buffer_format);
	struct bbox bounding = {
		.w = scale * width,
		.h = scale * height,
		.x = 0,
		.y = 0,
	};
	appsurface_init_buffer(&w->background, &w->pool, &bounding);

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
	//this is like an nk_egl_launch, but we have really bad implementation
	background->dirty[1] = true;
	appsurface_fadc(background);
}

static void
tw_panel_configure(void *data, struct tw_ui *tw_ui,
		   uint32_t width, uint32_t height, uint32_t scale)
{
	struct shell_output *output = data;
	struct app_surface *panel = &output->panel;
	struct desktop_shell *shell = output->shell;

	panel->w = width;
	panel->h = height;
	panel->s = scale;
	//TODO detect if we are on the major output, if not, we paint it differently
	output->panel_backend = nk_egl_create_backend(&output->shell->eglenv);
	struct shell_widget *widget;
	wl_list_for_each(widget, &shell->shell_widgets, link)
		if (widget->set_event_cb)
			widget->set_event_cb(widget,
					     output->panel_backend,
					     &shell->client_event_queue);
	nk_egl_set_theme(output->panel_backend, &taiwins_dark_theme);
	appsurface_init_egl(panel, &shell->eglenv);
	nk_egl_launch(output->panel_backend, panel, shell_panel_frame, output);

}

static struct tw_ui_listener tw_panel_impl = {
	.configure = tw_panel_configure
};

static struct tw_ui_listener tw_background_impl = {
	.configure = tw_background_configure
};


static void
initialize_shell_output(struct shell_output *w, struct tw_output *tw_output,
			struct desktop_shell *shell)
{
	w->shell = shell;
	w->output = tw_output;
	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *bg_ui =
		taiwins_shell_create_background(shell->shell, bg_sf, tw_output);
	appsurface_init(&w->background, NULL, APP_BACKGROUND, bg_sf,
			(struct wl_proxy *)bg_ui);
	tw_ui_add_listener(bg_ui, &tw_background_impl, w);

	struct wl_surface *pn_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *pn_ui =
		taiwins_shell_create_panel(shell->shell, pn_sf, tw_output);
	appsurface_init(&w->panel, NULL, APP_PANEL, pn_sf,
			(struct wl_proxy *)pn_ui);
	tw_ui_add_listener(pn_ui, &tw_panel_impl, w);

	//so we have one nk_egl_backend for the panel.

	tw_output_set_user_data(tw_output, w);
}

static void
release_shell_output(struct shell_output *w)
{
	struct app_surface *surfaces[] = {
		&w->panel,
		&w->background,
	};
	for (int i = 0; i < 2; i++)
		app_surface_release(surfaces[i]);

	//you may destroy the buffer before released
	shm_pool_release(&w->pool);
}




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

static inline int
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
	shell->widget_backend = nk_egl_create_backend(&shell->eglenv);
	nk_egl_set_theme(shell->widget_backend, &taiwins_dark_theme);

	wl_list_init(&shell->shell_widgets);
	wl_list_insert(&shell->shell_widgets, &clock_widget.link);

	tw_event_queue_init(&shell->client_event_queue);
	shell->client_event_queue.quit =
		!tw_event_queue_add_wl_display(&shell->client_event_queue, display);
	vector_init(&shell->uinit_outputs, sizeof(struct tw_output *), NULL);
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);

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

		initialize_shell_output(w, *(struct tw_output **)vector_at(&shell->uinit_outputs, j),
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
		shelloftaiwins = twshell->shell;
	} else if (!strcmp(interface, tw_output_interface.name)) {
		struct tw_output *tw_output =
			wl_registry_bind(wl_registry, name, &tw_output_interface, version);
		desktop_shell_try_add_tw_output(twshell, tw_output);
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
