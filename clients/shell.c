#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
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
#include "shell.h"

static struct desktop_shell oneshell; //singleton


/*******************************************************************************
 * shell_output apis
 ******************************************************************************/
static void
shell_output_set_major(struct shell_output *w)
{
	struct desktop_shell *shell = w->shell;

	if (shell->main_output == w)
		return;
	else if (shell->main_output)
		app_surface_release(&shell->main_output->panel);

	shell_init_panel_for_output(w);
	shell->main_output = w;
}

static void
shell_output_init(struct shell_output *w, const struct bbox geo, bool major)
{
	w->bbox = geo;
	shell_init_bg_for_output(w);

	if (major) {
		shell_output_set_major(w);
		app_surface_frame(&w->panel, false);
	}
	app_surface_frame(&w->background, false);
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
	shell_resize_bg_for_output(w);
	if (w == w->shell->main_output)
		shell_resize_panel_for_output(w);
}

/*******************************************************************************
 * desktop shell interface
 *
 * just know this code has side effect: it works even you removed and plug back
 * the output, since n_outputs returns at the first time it hits NULL. Even if
 * there is output afterwards, it won't know, so next time when you plug in
 * another monitor, it will choose the emtpy slots.
 ******************************************************************************/

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
	shell_process_msg(shell, type, arr);
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
	shell_widgets_load_default(&shell->shell_widgets);

	shell->widget_launch = (struct widget_launch_info){0};
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	tw_shell_destroy(shell->interface);

	struct shell_widget *widget, *tmp;
	wl_list_for_each_safe(widget, tmp, &shell->shell_widgets, link) {
		wl_list_remove(&widget->link);
		shell_widget_disactivate(widget, &shell->globals.event_queue);
	}

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
