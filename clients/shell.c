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
#include <poll.h>
#include <wayland-taiwins-desktop-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include <os/file.h>
#include <client.h>
#include <egl.h>
#include <nk_backends.h>
#include "../shared_config.h"
#include "widget.h"

struct shell_output {
	struct desktop_shell *shell;
	struct tw_output *output;
	//options
	struct {
		struct bbox bbox;
		int index;
	};

	struct app_surface background;
	struct app_surface panel;


	//a temporary struct
	double widgets_span;

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
	enum tw_shell_panel_pos panel_pos;
	//pannel configuration
	struct {
		struct nk_wl_backend *panel_backend;
		struct nk_style_button label_style;
		char wallpaper_path[128];
		//TODO calculated from font size
		size_t panel_height;
	};
	//widget configures
	struct {
		struct nk_wl_backend *widget_backend;
		struct wl_list shell_widgets;
		struct widget_launch_info widget_launch;
	};
	//outputs
	struct shell_output *main_output;
	struct shell_output shell_outputs[16];


} oneshell; //singleton


///////////////////////////////// background ////////////////////////////////////

static void
shell_background_frame(struct app_surface *surf, struct wl_buffer *buffer,
		       struct bbox *geo)

{
	//now it respond only app_surface_frame, we only need to add idle task
	//as for app_surface_frame later
	struct shell_output *output = container_of(surf, struct shell_output, background);
	struct desktop_shell *shell = output->shell;
	*geo = surf->allocation;
	void *buffer_data = shm_pool_buffer_access(buffer);
	if (!strlen(shell->wallpaper_path))
		sprintf(shell->wallpaper_path,
			"%s/.wallpaper/wallpaper.png", getenv("HOME"));
	if (load_image(shell->wallpaper_path, surf->pool->format,
		       surf->allocation.w*surf->allocation.s,
		       surf->allocation.h*surf->allocation.s,
		       (unsigned char *)buffer_data) != buffer_data) {
		fprintf(stderr, "failed to load image somehow\n");
	}
}

//////////////////////////////// widget ///////////////////////////////////

static void
widget_should_close(void *data, struct tw_ui *ui_elem)
{
	struct widget_launch_info *info = (struct widget_launch_info *)data;
	struct shell_widget *widget = info->current;
	app_surface_release(&widget->widget);
	info->current = NULL;
}

static struct  tw_ui_listener widget_impl = {
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
							    shell_output->index,
							    info->x, info->y);
	tw_ui_add_listener(widget_proxy, &widget_impl, info);
	//launch widget
	app_surface_init(&info->widget->widget, widget_surface,
			 (struct wl_proxy *)widget_proxy, panel_surf->wl_globals);
	nk_cairo_impl_app_surface(&info->widget->widget, shell->widget_backend,
				  info->widget->draw_cb,
				  make_bbox(info->x, info->y,
					    info->widget->w, info->widget->h,
					    shell_output->bbox.s),
				  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER);

	app_surface_frame(&info->widget->widget, false);

	info->current = info->widget;
}

//////////////////////////////// panel ////////////////////////////////////
static inline struct nk_vec2
widget_launch_point_flat(struct nk_vec2 *label_span, struct shell_widget *clicked,
			 struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	int w = panel_surf->allocation.w;
	int h = panel_surf->allocation.h;
	struct nk_vec2 info;
	if (label_span->x + clicked->w > w)
		info.x = w - clicked->w;
	else if (label_span->y - clicked->w < 0)
		info.x = label_span->x;
	else
		info.x = label_span->x;
	//this totally depends on where the panel is
	if (shell_output->shell->panel_pos == TW_SHELL_PANEL_POS_TOP)
		info.y = h;
	else {
		info.y = shell_output->bbox.h -
			panel_surf->allocation.h -
			clicked->h;
	}
	return info;
}

static void
shell_panel_measure_leading(struct nk_context *ctx, float width, float height,
			    struct app_surface *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	nk_text_width_f text_width = ctx->style.font->width;
	struct shell_widget_label widget_label;
	struct shell_widget *widget = NULL;

	double total_width = 0.0;
	int h = panel_surf->allocation.h;
	size_t n_widgets =  wl_list_length(&shell->shell_widgets);
	nk_layout_row_begin(ctx, NK_STATIC, h - 12, n_widgets);
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

	int h = panel_surf->allocation.h;
	int w = panel_surf->allocation.w;

	nk_layout_row_begin(ctx, NK_STATIC, h-12, n_widgets+1);
	int leading = w - (int)(shell_output->widgets_span+0.5)-20;
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
		nk_button_text_styled(ctx, &shell_output->shell->label_style,
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

///////////////////////////////////////////////////////////////////////////

static void
shell_output_set_major(struct shell_output *w)
{
	struct desktop_shell *shell = w->shell;
	struct wl_surface *pn_sf;
	struct tw_ui *pn_ui;
	if (shell->main_output == w)
		return;
	else if (shell->main_output)
		app_surface_release(&shell->main_output->panel);

	//at this point, we are  sure to create the resource
	pn_sf = wl_compositor_create_surface(shell->globals.compositor);
	pn_ui = tw_shell_create_panel(shell->interface, pn_sf, w->index);
	app_surface_init(&w->panel, pn_sf, (struct wl_proxy *)pn_ui,
			 &shell->globals);
	nk_cairo_impl_app_surface(&w->panel, shell->panel_backend, shell_panel_frame,
				  make_bbox_origin(w->bbox.w, shell->panel_height, w->bbox.s),
				  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER);

	struct shell_widget *widget;
	wl_list_for_each(widget, &shell->shell_widgets, link) {
		shell_widget_hook_panel(widget, &w->panel);
	}

	shell->main_output = w;
}


static void
shell_output_init(struct shell_output *w, const struct bbox geo, bool major)
{
	struct desktop_shell *shell = w->shell;
	w->bbox = geo;
	//background
	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *bg_ui =
		tw_shell_create_background(shell->interface, bg_sf, w->index);
	app_surface_init(&w->background, bg_sf, (struct wl_proxy *)bg_ui,
			 &shell->globals);
	shm_buffer_impl_app_surface(&w->background,
				    shell_background_frame,
				    w->bbox);
	//shell panel
	if (major)
		shell_output_set_major(w);
	app_surface_frame(&w->background, false);
	if (major) {
		nk_wl_test_draw(shell->panel_backend, &w->panel,
				shell_panel_measure_leading);
		app_surface_frame(&w->panel, false);
	}
}


static void
shell_output_release(struct shell_output *w)
{
	w->shell = NULL;
	struct app_surface *surfaces[] = {
		&w->panel,
		&w->background,
	};
	for (int i = 0; i < 2; i++)
		if (surfaces[i]->wl_surface)
			app_surface_release(surfaces[i]);
}

static void
shell_output_resize(struct shell_output *w, const struct bbox geo)
{
	w->bbox = geo;
	app_surface_resize(&w->background, w->bbox.w, w->bbox.h, w->bbox.s);
	if (w == w->shell->main_output) {
		nk_wl_test_draw(w->shell->panel_backend, &w->panel,
				shell_panel_measure_leading);
		app_surface_resize(&w->panel, w->bbox.w, w->shell->panel_height,
				   w->bbox.s);
	}
}

/************************** desktop_shell_interface ********************************/
/* just know this code has side effect: it works even you removed and plug back
 * the output, since n_outputs returns at the first time it hits NULL. Even if
 * there is output afterwards, it won't know, so next time when you plug in
 * another monitor, it will choose the emtpy slots.
 */
static inline int
desktop_shell_n_outputs(struct desktop_shell *shell)
{
	for (int i = 0; i < 16; i++)
		if (shell->shell_outputs[i].shell == NULL)
			return i;
	return 16;
}

static vector_t
taiwins_menu_from_wl_array(struct wl_array *serialized)
{
	vector_t dst;
	vector_t src;
	vector_init_zero(&dst, sizeof(struct taiwins_menu_item), NULL);
	src = dst;
	src.alloc_len = serialized->size / (sizeof(struct taiwins_menu_item));
	src.len = src.alloc_len;
	src.elems = serialized->data;
	vector_copy(&dst, &src);
	return dst;
}

static void
desktop_shell_setup_menu(struct desktop_shell *shell,
			 struct wl_array *serialized)
{
	vector_t menus = taiwins_menu_from_wl_array(serialized);
	//what you do here? you can validate it, then copy to shell.
	vector_destroy(&menus);
}

static void
desktop_shell_setup_wallpaper(struct desktop_shell *shell, const char *path)
{
	if (is_file_exist(path))
		strncpy(shell->wallpaper_path, path, 127);
	for (int i = 0; i < desktop_shell_n_outputs(shell); i++) {
		struct app_surface *bg =
			&shell->shell_outputs[i].background;
		if (bg->wl_surface)
			app_surface_frame(bg, false);
	}
}

//right now we are using switch, but we can actually use a table, since we make
//the msg_type a continues field.
static void
desktop_shell_recv_msg(void *data,
		       struct tw_shell *tw_shell,
		       uint32_t type,
		       struct wl_array *arr)
{
	/* right now I think string is okay, but later it may get inefficient */
	struct desktop_shell *shell = data;
	union wl_argument arg;

	switch (type) {
	case TW_SHELL_MSG_TYPE_NOTIFICATION:
		arg.s = arr->data;
		break;
	case TW_SHELL_MSG_TYPE_PANEL_POS:
		arg.u = atoi((const char*)arr->data);
		shell->panel_pos = arg.u == TW_SHELL_PANEL_POS_TOP ?
			TW_SHELL_PANEL_POS_TOP : TW_SHELL_PANEL_POS_BOTTOM;
		break;
	case TW_SHELL_MSG_TYPE_MENU:
		desktop_shell_setup_menu(shell, arr);
		break;
	case TW_SHELL_MSG_TYPE_WALLPAPER:
		desktop_shell_setup_wallpaper(shell, (const char *)arr->data);
		break;
	case TW_SHELL_MSG_TYPE_SWITCH_WORKSPACE:
	{
		fprintf(stderr, "switch workspace\n");
		break;
	}
	default:
		break;
	}
}

static void
desktop_shell_output_configure(void *data, struct tw_shell *tw_shell,
			       uint32_t id, uint32_t width, uint32_t height,
			       uint32_t scale, uint32_t major, uint32_t msg)
{
	struct desktop_shell *shell = data;
	struct shell_output *output = &shell->shell_outputs[id];
	struct bbox geometry =  make_bbox_origin(width, height, scale);
	output->shell = shell;
	output->index = id;
	switch (msg) {
	case TW_SHELL_OUTPUT_MSG_CONNECTED:
		shell_output_init(output, geometry, major);
		break;
	case TW_SHELL_OUTPUT_MSG_CHANGE:
		shell_output_resize(output, geometry);
		break;
	case TW_SHELL_OUTPUT_MSG_LOST:
		shell_output_release(output);
		break;
	default:
		break;
	}
	output->bbox = make_bbox_origin(width, height, scale);
}

static struct tw_shell_listener tw_shell_impl = {
	.output_configure = desktop_shell_output_configure,
	.shell_msg = desktop_shell_recv_msg,
};



static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	struct nk_style_button *style = &shell->label_style;

	wl_globals_init(&shell->globals, display);
	shell->globals.theme = taiwins_dark_theme;
	shell->interface = NULL;
	shell->panel_height = 32;
	shell->main_output = NULL;
	shell->wallpaper_path[0] = '\0';

	shell->widget_backend = nk_cairo_create_bkend();
	shell->panel_backend = nk_cairo_create_bkend();
	{
		const struct nk_style *theme =
			nk_wl_get_curr_style(shell->panel_backend);
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

	//right now we just hard coded some link
	//add the widgets here
	wl_list_init(&shell->shell_widgets);
	wl_list_insert(&shell->shell_widgets, &clock_widget.link);
	wl_list_insert(&shell->shell_widgets, &what_up_widget.link);
	wl_list_insert(&shell->shell_widgets, &battery_widget.link);

	shell_widget_activate(&clock_widget, &shell->globals.event_queue);
	shell_widget_activate(&what_up_widget, &shell->globals.event_queue);
	shell_widget_activate(&battery_widget, &shell->globals.event_queue);
	shell->widget_launch = (struct widget_launch_info){0};
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	tw_shell_destroy(shell->interface);

	for (int i = 0; i < desktop_shell_n_outputs(shell); i++)
		shell_output_release(&shell->shell_outputs[i]);
	wl_globals_release(&shell->globals);
	//destroy the backends
	nk_cairo_destroy_bkend(shell->widget_backend);
	nk_cairo_destroy_bkend(shell->panel_backend);
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
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
		tw_shell_add_listener(twshell->interface, &tw_shell_impl, twshell);
	}
	else
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

	wl_display_flush(display);
	wl_globals_dispatch_event_queue(&oneshell.globals);
	//clear up
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return 0;
}
