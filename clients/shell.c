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

#include <wayland-taiwins-desktop-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include <client.h>
#include <egl.h>
#include "widget.h"
#include <nk_backends.h>

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	//options
	struct {
		struct bbox bbox;
		unsigned int scale;
		off_t index;
	};

	struct app_surface background;
	struct app_surface panel;

	struct nk_style_button label_style;

	//a temporary struct
	double widgets_span;

	struct nk_wl_backend *panel_backend;
	struct shm_pool pool;
};

//state of current widget and widget to launch
struct widget_launch_info {
	uint32_t x;
	uint32_t y;
	struct shell_widget *widget;
	struct shell_widget *current;
};


struct desktop_shell {
	struct wl_globals globals;
	struct tw_shell *interface;
	//you have about 16 outputs to spear
	struct shell_output shell_outputs[16];
	off_t main_output;

	struct nk_wl_backend *widget_backend;
	struct shm_pool pool;
	struct wl_list shell_widgets;
	struct widget_launch_info widget_launch;
} oneshell; //singleton


///////////////////////////////// background ////////////////////////////////////

static void
shell_background_frame(struct app_surface *surf, struct wl_buffer *buffer,
		 int32_t *dx, int32_t *dy, int32_t *dw, int32_t *dh)
{
	//now it respond only app_surface_frame, we only need to add idle task
	//as for app_surface_frame later
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
shell_background_configure(void *data,
			struct tw_ui *tw_ui,
			uint32_t width,
			uint32_t height)
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
shell_background_should_close(void *data, struct tw_ui *ui_elem)
{
	//TODO, destroy the surface
}



//////////////////////////////// widget ///////////////////////////////////
static void
widget_configure(void *data, struct tw_ui *ui_elem,
		 uint32_t width, uint32_t height) {}

static void
widget_should_close(void *data, struct tw_ui *ui_elem)
{
	struct widget_launch_info *info = (struct widget_launch_info *)data;
	struct shell_widget *widget = info->current;
	app_surface_release(&widget->widget);
	info->current = NULL;
}

static struct  tw_ui_listener widget_impl = {
	.configure = widget_configure,
	.close = widget_should_close,
};

//later we can take advantage of the idle queue for this.
void
launch_widget(struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	struct widget_launch_info *info = &shell->widget_launch;
	if (info->current == info->widget)
		return;
	else if (info->current != NULL) {
		//if there is a widget launched and is not current widget
		app_surface_release(&info->current->widget);
		info->current = NULL;
	}
	/* info->widget->widget.wl_globals = panel_surf->wl_globals; */
	struct wl_surface *widget_surface = wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *widget_proxy = tw_shell_launch_widget(shell->interface, widget_surface,
							    shell_output->output,
							    info->x, info->y);
	tw_ui_add_listener(widget_proxy, &widget_impl, info);
	//launch widget
	app_surface_init(&info->widget->widget, widget_surface,
			 (struct wl_proxy *)widget_proxy, panel_surf->wl_globals);
	nk_cairo_impl_app_surface(&info->widget->widget, shell->widget_backend,
				  info->widget->draw_cb, &shell->pool,
				  info->widget->w, info->widget->h, info->x, info->y,
				  shell_output->scale,
				  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER);

	app_surface_frame(&info->widget->widget, false);

	info->current = info->widget;
}


//////////////////////////////// panel ////////////////////////////////////
static inline struct nk_vec2
widget_launch_point_flat(struct nk_vec2 *label_span, struct shell_widget *clicked,
			 struct app_surface *panel_surf)
{
	struct nk_vec2 info;
	if (label_span->x + clicked->w > panel_surf->w)
		info.x = panel_surf->w - clicked->w;
	else if (label_span->y - clicked->w < 0)
		info.x = label_span->x;
	else
		info.x = label_span->x;
	info.y = panel_surf->h;
	return info;
}

static void
shell_panel_measure_leading(struct nk_context *ctx, float width, float height, struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	nk_text_width_f text_width = ctx->style.font->width;
	struct shell_widget_label widget_label;
	struct shell_widget *widget = NULL;

	double total_width = 0.0;
	size_t n_widgets =  wl_list_length(&shell->shell_widgets);
	nk_layout_row_begin(ctx, NK_STATIC, panel_surf->h - 12, n_widgets);
	wl_list_for_each(widget, &shell->shell_widgets, link) {
		int len = widget->ancre_cb(widget, &widget_label);
		double width =
			text_width(ctx->style.font->userdata,
				   ctx->style.font->height,
				   widget_label.label, len);
		nk_layout_row_push(ctx, width+10);
		/* struct nk_rect bound = nk_widget_bounds(ctx); */
		nk_button_text(ctx, widget_label.label, len);
		total_width += width+10;
	}
	shell_output->widgets_span = total_width;
}

static void
shell_panel_frame(struct nk_context *ctx, float width, float height, struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	struct shell_widget_label widget_label;
	nk_text_width_f text_width = ctx->style.font->width;
	//drawing labels
	size_t n_widgets =  wl_list_length(&shell->shell_widgets);
	struct shell_widget *widget = NULL, *clicked = NULL;
	struct nk_vec2 label_span = nk_vec2(0, 0);

	nk_layout_row_begin(ctx, NK_STATIC, panel_surf->h-12, n_widgets+1);
	int leading = panel_surf->w - (int)(shell_output->widgets_span+0.5)-20;
	nk_layout_row_push(ctx, leading);
	nk_spacing(ctx, 1);

	wl_list_for_each(widget, &shell->shell_widgets, link) {
		int len = widget->ancre_cb(widget, &widget_label);
		double width =
			text_width(ctx->style.font->userdata,
				   ctx->style.font->height,
				   widget_label.label, len);

		nk_layout_row_push(ctx, width+10);
		struct nk_rect bound = nk_widget_bounds(ctx);
		if (nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT)) {
			clicked = widget;
			label_span.x = bound.x;
			label_span.y = bound.x+bound.w;
		}
		nk_button_text_styled(ctx, &shell_output->label_style,
				      widget_label.label, len);
	}
	nk_layout_row_end(ctx);
	//check if widget is already launched
	if (!clicked || clicked->widget.protocol || !clicked->draw_cb)
		return;
	struct widget_launch_info *info = &shell->widget_launch;
	info->widget = clicked;
	struct nk_vec2 p = widget_launch_point_flat(&label_span, clicked, panel_surf);
	info->x = (int)p.x;
	info->y = (int)p.y;
	nk_wl_add_idle(ctx, launch_widget);
}


/*
static void
shell_panel_update(struct app_surface *surf, uint32_t user_data)
{
	//In other case. You may want to draw other stuff, you will want to
	//clean the texture.  using api like nk_wl_clean_canvas(surf). Then we
	//know if it is clean. What if you want to have opengl draw calls? It is
	//possible, you can create the program once ctx is created.
	struct nk_context *ctx = surf->user_data->ctx;
	if (nk_begin(ctx, "panel", nk_rect(0, 0, surf->w, surf->h),
		     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
		shell_panel_frame(ctx, surf->w, surf->h, surf);
	} nk_end(ctx);
	nk_wl_render(ctx->user_data);
}
*/

static void
shell_panel_configure(void *data, struct tw_ui *tw_ui,
		   uint32_t width, uint32_t height)
{
	struct shell_output *output = data;
	struct app_surface *panel = &output->panel;
	struct desktop_shell *shell = output->shell;
	struct nk_style_button *style = &output->label_style;

	//major output is handled some where else
	output->panel_backend =  nk_cairo_create_bkend();
	struct shell_widget *widget;
	wl_list_for_each(widget, &shell->shell_widgets, link)
		shell_widget_activate(widget, panel, &shell->globals.event_queue);

	nk_cairo_impl_app_surface(panel, output->panel_backend, shell_panel_frame,
				  &output->pool, width, height, 0, 0, output->scale,
				  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER);
	/* nk_egl_impl_app_surface(panel, output->panel_backend, shell_panel_frame, */
	/*			width, height, 0 ,0); */
	nk_wl_test_draw(output->panel_backend, panel, shell_panel_measure_leading);
	{
		const struct nk_style *theme =
			nk_wl_get_curr_style(output->panel_backend);
		memcpy(style, &theme->button, sizeof(struct nk_style_button));
		struct nk_color text_normal = theme->button.text_normal;
		style->normal = nk_style_item_color(theme->window.background);
		style->hover = nk_style_item_color(theme->window.background);
		style->active = nk_style_item_color(theme->window.background);
		style->border_color = theme->window.background;
		style->text_background = theme->window.background;
		style->text_normal = text_normal;
		style->text_hover = nk_rgba(text_normal.r + 20, text_normal.g + 20,
					    text_normal.b + 20, text_normal.a);
		style->text_active = nk_rgba(text_normal.r + 40, text_normal.g + 40,
					     text_normal.b + 40, text_normal.a);
	}

	app_surface_frame(panel, false);

}

static struct tw_ui_listener shell_panel_impl = {
	.configure = shell_panel_configure
};

static struct tw_ui_listener shell_background_impl = {
	.configure = shell_background_configure,
	.close = shell_background_should_close,
};


///////////////////////////////////////////////////////////////////////////

static void
shell_output_add_shell_desktop(struct shell_output *w,
			       struct desktop_shell *shell, bool main_output)
{
	w->shell = shell;

	if (w->background.wl_surface)
		app_surface_release(&w->background);

	struct app_surface *bg = &w->background;
	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *bg_ui =
		tw_shell_create_background(shell->interface, bg_sf, w->output);
	app_surface_init(bg, bg_sf, (struct wl_proxy *)bg_ui,
			 &shell->globals);
	tw_ui_add_listener(bg_ui, &shell_background_impl, w);
	//only add panel for main output
	if (main_output) {
		if (w->panel.wl_surface)
			app_surface_release(&w->panel);
		struct wl_surface *pn_sf =
			wl_compositor_create_surface(shell->globals.compositor);
		struct tw_ui *pn_ui =
			tw_shell_create_panel(shell->interface, pn_sf, w->output);
		app_surface_init(&w->panel, pn_sf, (struct wl_proxy *)pn_ui,
				 &shell->globals);

		tw_ui_add_listener(pn_ui, &shell_panel_impl, w);
	}
}


static void
shell_output_configure(void *data,
		       struct tw_output *tw_output,
		       uint32_t width,
		       uint32_t height,
		       uint32_t scale,
		       int32_t x,
		       int32_t y,
		       uint32_t major)
{
	struct shell_output *w = data;
	//this logic here is to descover any changes
	bool need_update = w->bbox.w != width || w->bbox.h != height || w->scale != scale ||
		major ^ (w->index == w->shell->main_output);
	w->bbox.x = x; w->bbox.y = y;
	w->bbox.w = width; w->bbox.h = height;
	w->scale = scale;

	w->shell->main_output = (major) ? w->index : w->shell->main_output;
	//in the initial step, this actually never called
	if (w->shell->interface && need_update)
		shell_output_add_shell_desktop(w, w->shell, major);
}


static struct tw_output_listener shell_output_impl = {
	.configure = shell_output_configure,
};


static void
shell_output_init(struct shell_output *w, struct tw_output *tw_output)
{
	w->shell = NULL;
	w->output = tw_output;
	tw_output_add_listener(tw_output, &shell_output_impl, w);
	w->bbox = (struct bbox) {0,0, 1000 ,1000};
	w->scale = 1;
}


static void
shell_output_release(struct shell_output *w)
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
	return shell->interface && is_shm_format_valid(shell->globals.buffer_format);
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

static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	wl_globals_init(&shell->globals, display);
	shell->globals.theme = taiwins_dark_theme;
	shell->interface = NULL;

	shell->widget_backend = nk_cairo_create_bkend();
	//right now we just hard coded some link
	wl_list_init(&shell->shell_widgets);
	wl_list_insert(&shell->shell_widgets, &clock_widget.link);
	wl_list_insert(&shell->shell_widgets, &what_up_widget.link);
	wl_list_insert(&shell->shell_widgets, &battery_widget.link);

}


static void
desktop_shell_release(struct desktop_shell *shell)
{
	tw_shell_destroy(shell->interface);

	for (int i = 0; i < desktop_shell_n_outputs(shell); i++)
		shell_output_release(&shell->shell_outputs[i]);
	wl_globals_release(&shell->globals);
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}

static inline void
desktop_shell_add_tw_output(struct desktop_shell *shell, struct tw_output *tw_output)
{
	int n = desktop_shell_n_outputs(shell);
	shell_output_init(&shell->shell_outputs[n], tw_output);
	shell->shell_outputs[n].index = n;
	shell->shell_outputs[n].shell = shell;
}

static void
desktop_shell_prepare(struct desktop_shell *shell)
{
	//TODO we should change the logic here, if
	for (int i = 0; i < desktop_shell_n_outputs(shell); i++)
		shell_output_add_shell_desktop(&shell->shell_outputs[i], shell,
					       shell->main_output == i);
	//widget buffer(since we are using cairo for rendering)
	shm_pool_init(&shell->pool, shell->globals.shm, 4096,
		      shell->globals.buffer_format);
	shell->widget_launch = (struct widget_launch_info){0};
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

	if (strcmp(interface, tw_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		twshell->interface = (struct tw_shell *)
			wl_registry_bind(wl_registry, name, &tw_shell_interface, version);
	} else if (!strcmp(interface, tw_output_interface.name)) {
		struct tw_output *tw_output =
			wl_registry_bind(wl_registry, name, &tw_output_interface, version);
		desktop_shell_add_tw_output(twshell, tw_output);
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
	//we should delete every thing and process here, it is easier
	desktop_shell_prepare(&oneshell);

	wl_display_flush(display);
	wl_globals_dispatch_event_queue(&oneshell.globals);
	//clear up
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return 0;
}
