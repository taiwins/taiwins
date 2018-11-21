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
#include "nuklear/nk_wl_egl.h"
#include "widget.h"

struct taiwins_shell *shelloftaiwins;

struct widget_launch_info {
	uint32_t x;
	uint32_t y;
	struct shell_widget *widget;
};

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	//for right now we just need to draw the icons from left to right. Later
	//we may have better solution
	struct app_surface background;
	struct app_surface panel;
	//a temporary struct
	struct widget_launch_info widget_launch;

	struct nk_egl_backend *panel_backend;
	struct shm_pool pool;
};

struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	struct egl_env eglenv;
	struct egl_env widget_env;

	struct shell_output shell_outputs[16];

	struct nk_egl_backend *widget_backend;
	struct wl_list shell_widgets;
	struct tw_event_queue client_event_queue;
	//states
	vector_t uinit_outputs;
	bool quit;
} oneshell; //singleton

struct tw_event_queue *the_event_processor = &oneshell.client_event_queue;



///////////////////////////////// background ////////////////////////////////////

static void
shell_background_frame(struct app_surface *surf, struct wl_buffer *buffer,
		 int32_t *dx, int32_t *dy, int32_t *dw, int32_t *dh)
{
	*dx = 0;
	*dy = 0;
	*dw = surf->w;
	*dh = surf->h;
	void *buffer_data = shm_pool_buffer_access(buffer);
	char imgpath[100];

	sprintf(imgpath, "%s/.wallpaper/wallpaper.png", getenv("HOME"));
	if (load_image(imgpath, surf->pool->format, surf->w, surf->h,
		       (unsigned char *)buffer_data) != buffer_data) {
		fprintf(stderr, "failed to load image somehow\n");
	}
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

	shm_pool_init(&w->pool, shell->globals.shm, 4096, shell->globals.buffer_format);
	shm_buffer_impl_app_surface(background, &w->pool, shell_background_frame,
				    width, height);
	app_surface_frame(background, false);
}

static void
tw_background_should_close(void *data, struct tw_ui *ui_elem)
{
	//TODO, destroy the surface
}

//////////////////////////////// widget ///////////////////////////////////
static void
widget_configure(void *data, struct tw_ui *ui_elem,
		 uint32_t width, uint32_t height, uint32_t scale) {}

static void
widget_should_close(void *data, struct tw_ui *ui_elem)
{
	struct shell_widget *widget = (struct shell_widget *)data;
	app_surface_release(&widget->widget);
}

static struct  tw_ui_listener widget_impl = {
	.configure = widget_configure,
	.close = widget_should_close,
};


void
launch_widget(struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct widget_launch_info *info = &shell_output->widget_launch;
	struct desktop_shell *shell = shell_output->shell;
	info->widget->widget.wl_globals = panel_surf->wl_globals;
	struct wl_surface *widget_surface = wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *widget_proxy = taiwins_shell_launch_widget(shell->shell, widget_surface,
								 shell_output->output,
								 info->x, info->y);
	tw_ui_add_listener(widget_proxy, &widget_impl, info->widget);
	/* we should release the previous surface as well */
	shell_widget_launch(info->widget, widget_surface, (struct wl_proxy *)widget_proxy,
			    shell->widget_backend,
			    info->x, info->y);
	*info = (struct widget_launch_info){0};
}


//////////////////////////////// panel ////////////////////////////////////


static void
shell_panel_frame(struct nk_context *ctx, float width, float height, struct app_surface *panel_surf)
{
	enum nk_buttons button;
	uint32_t sx, sy;
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;

	//a temporary hack
	static int no_widget = 0;
	//actual drawing
	int i = 0;
	size_t n_widgets =  wl_list_length(&shell->shell_widgets);
	struct shell_widget *widget = NULL, *clicked = NULL;
	nk_layout_row_begin(ctx, NK_STATIC, panel_surf->h - 12, n_widgets);
	wl_list_for_each(widget, &shell->shell_widgets, link) {
		nk_layout_row_push(ctx, 100);
		//nk_widget_is_mouse_clicked need to be after row_push and
		//before actually call to the widget.

		//happen automatically for nk_layout_row_static or
		//nk_layout_row_dynamic. So if you do not know how wide the
		//widget occupies, there is nothing you can do.
		if (nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT))
			clicked = widget;
		widget->ancre_cb(ctx, width, height, &widget->ancre);
		i++;
	}
	nk_layout_row_end(ctx);
	if (!clicked)
		return;
	//TODO remove this two lines
	no_widget += 1;
	no_widget = no_widget % n_widgets;

	struct widget_launch_info *info = &shell_output->widget_launch;
	nk_egl_get_btn(ctx, &button, &sx, &sy);
	info->widget = clicked;
	//determine the launch point
	if (sx + clicked->widget.w/2 >= panel_surf->w)
		info->x = panel_surf->w - clicked->widget.w;
	else if (sx - clicked->widget.w/2 < 0)
		info->x = 0;
	else
		info->x = sx - clicked->widget.w/2;
	info->y = sy;

	//again, we should add a post cb here.
	if (clicked->widget.protocol || !clicked->draw_cb)
		return;
	nk_egl_add_idle(ctx, launch_widget);
}



static void
tw_panel_configure(void *data, struct tw_ui *tw_ui,
		   uint32_t width, uint32_t height, uint32_t scale)
{
	struct shell_output *output = data;
	struct app_surface *panel = &output->panel;
	struct desktop_shell *shell = output->shell;

	//TODO detect if we are on the major output. If not, we do not add the
	//widgets, and draw call should be different
	output->panel_backend = nk_egl_create_backend(&output->shell->eglenv);
	struct shell_widget *widget;
	wl_list_for_each(widget, &shell->shell_widgets, link)
		shell_widget_activate(widget, panel, &shell->client_event_queue);

	nk_egl_impl_app_surface(panel, output->panel_backend, shell_panel_frame,
				width, height, 0 ,0);
	app_surface_frame(panel, false);

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
	struct app_surface *bg = &w->background;
	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *bg_ui =
		taiwins_shell_create_background(shell->shell, bg_sf, tw_output);
	app_surface_init(bg, bg_sf, (struct wl_proxy *)bg_ui);
	bg->wl_globals = &shell->globals;
	tw_ui_add_listener(bg_ui, &tw_background_impl, w);

	struct wl_surface *pn_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *pn_ui =
		taiwins_shell_create_panel(shell->shell, pn_sf, tw_output);
	app_surface_init(&w->panel, pn_sf, (struct wl_proxy *)pn_ui);
	w->panel.wl_globals = &shell->globals;
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
	shell->globals.theme = taiwins_dark_theme;
	shell->shell = NULL;
	shell->quit = false;
	egl_env_init(&shell->eglenv, display);
	egl_env_init(&shell->widget_env, display);
	shell->widget_backend = nk_egl_create_backend(&shell->widget_env);

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
	egl_env_end(&shell->widget_env);
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
