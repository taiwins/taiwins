/*
 * shell.c - taiwins client shell implementation
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <cairo/cairo.h>
#include <poll.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include <os/file.h>
#include <client.h>
#include <egl.h>
#include <nk_backends.h>
#include <wayland-util.h>
#include "../shared_config.h"
#include "theme.h"
#include "vector.h"
#include "wayland-taiwins-theme-client-protocol.h"
#include "wayland-taiwins-theme-server-protocol.h"
#include "widget.h"
#include "shell.h"



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
		tw_appsurf_release(&shell->main_output->panel);

	shell_init_panel_for_output(w);
	shell->main_output = w;
}

static void
shell_output_init(struct shell_output *w, const struct tw_bbox geo, bool major)
{
	w->bg_ui = NULL;
	w->pn_ui = NULL;
	w->bbox = geo;
	shell_init_bg_for_output(w);

	if (major) {
		shell_output_set_major(w);
		tw_appsurf_frame(&w->panel, false);
	}
	tw_appsurf_frame(&w->background, false);
}

static void
shell_output_release(struct shell_output *w)
{
	struct {
		struct tw_appsurf *app;
		struct taiwins_ui *protocol;
	} uis[] = {
		{&w->panel, w->bg_ui},
		{&w->background, w->pn_ui}
	};

	for (int i = 0; i < 2; i++)
		if (uis[i].protocol) {
			taiwins_ui_destroy(uis[i].protocol);
			tw_appsurf_release(uis[i].app);
		}
	w->shell = NULL;

}

static void
shell_output_resize(struct shell_output *w, const struct tw_bbox geo)
{
	w->bbox = geo;
	shell_resize_bg_for_output(w);
	if (w == w->shell->main_output)
		shell_resize_panel_for_output(w);
}

/*******************************************************************************
 * taiwins_theme_interface

 * just know this code has side effect: it works even you removed and plug back
 * the output, since n_outputs returns at the first time it hits NULL. Even if
 * there is output afterwards, it won't know, so next time when you plug in
 * another monitor, it will choose the emtpy slots.
 ******************************************************************************/

static void
desktop_shell_recv_msg(void *data,
		       struct taiwins_shell *tw_shell,
		       uint32_t type,
		       struct wl_array *arr)
{
	/* right now I think string is okay, but later it may get inefficient */
	struct desktop_shell *shell = data;
	shell_process_msg(shell, type, arr);
}

static void
desktop_shell_output_configure(void *data, struct taiwins_shell *tw_shell,
			       uint32_t id, uint32_t width, uint32_t height,
			       uint32_t scale, uint32_t major, uint32_t msg)
{
	struct desktop_shell *shell = data;
	struct shell_output *output = &shell->shell_outputs[id];
	struct tw_bbox geometry =  tw_make_bbox_origin(width, height, scale);
	output->shell = shell;
	output->index = id;
	switch (msg) {
	case TAIWINS_SHELL_OUTPUT_MSG_CONNECTED:
		shell_output_init(output, geometry, major);
		break;
	case TAIWINS_SHELL_OUTPUT_MSG_CHANGE:
		shell_output_resize(output, geometry);
		break;
	case TAIWINS_SHELL_OUTPUT_MSG_LOST:
		shell_output_release(output);
		break;
	default:
		break;
	}
	output->bbox = tw_make_bbox_origin(width, height, scale);
}

static struct taiwins_shell_listener tw_shell_impl = {
	.output_configure = desktop_shell_output_configure,
	.shell_msg = desktop_shell_recv_msg,
};

static const struct nk_wl_font_config icon_config = {
	.name = "icons",
	.slant = NK_WL_SLANT_ROMAN,
	.pix_size = 16,
	.scale = 1,
	.TTFonly = false,
};

/*******************************************************************************
 * taiwins_theme_interface
 ******************************************************************************/

static void
desktop_shell_apply_theme(void *data,
                          struct taiwins_theme *taiwins_theme,
                          const char *name,
                          int32_t fd,
                          uint32_t size)
{
	struct desktop_shell *shell = data;

	tw_theme_fini(&shell->theme);
	tw_theme_init_from_fd(&shell->theme, fd, size);
}


static const struct taiwins_theme_listener tw_theme_impl = {
	.theme = desktop_shell_apply_theme,
};

/*******************************************************************************
 * desktop_shell_interface
 ******************************************************************************/

static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	struct nk_style_button *style = &shell->label_style;

	tw_globals_init(&shell->globals, display);
	shell_tdbus_init(shell);
	tw_theme_init_default(&shell->theme);

	shell->globals.theme = &shell->theme;
	shell->interface = NULL;
	shell->panel_height = 32;
	shell->main_output = NULL;
	shell->wallpaper_path[0] = '\0';

	shell->widget_backend = nk_cairo_create_backend();
	shell->panel_backend = nk_cairo_create_backend();
	shell->icon_font = nk_wl_new_font(icon_config, shell->panel_backend);
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
		style->text_hover = nk_rgba(text_normal.r + 20,
		                            text_normal.g + 20,
					    text_normal.b + 20,
		                            text_normal.a);
		style->text_active = nk_rgba(text_normal.r + 40,
		                             text_normal.g + 40,
					     text_normal.b + 40,
		                             text_normal.a);
	}

	//widgets
	wl_list_init(&shell->shell_widgets);
	shell_widgets_load_default(&shell->shell_widgets);

	shell->widget_launch = (struct widget_launch_info){0};

	//notifications
	wl_list_init(&shell->notifs.msgs);
	tw_signal_init(&shell->notifs.msg_recv_signal);
	tw_signal_init(&shell->notifs.msg_del_signal);

	//menu
	vector_init_zero(&shell->menu, sizeof(struct tw_menu_item), NULL);
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->interface);

	struct shell_widget *widget, *tmp;
	wl_list_for_each_safe(widget, tmp, &shell->shell_widgets, link) {
		wl_list_remove(&widget->link);
		shell_widget_disactivate(widget, &shell->globals.event_queue);
	}

	for (int i = 0; i < desktop_shell_n_outputs(shell); i++)
		shell_output_release(&shell->shell_outputs[i]);

	shell_tdbus_end(shell);
	tw_globals_release(&shell->globals);
	//destroy the backends
	nk_cairo_destroy_backend(shell->widget_backend);
	nk_cairo_destroy_backend(shell->panel_backend);
	tw_theme_fini(&shell->theme);
	//destroy left over notifications
	shell_cleanup_notifications(shell);
	vector_destroy(&shell->menu);

#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}

/*******************************************************************************
 * globals
 ******************************************************************************/

static void
announce_globals(void *data,
                 struct wl_registry *wl_registry,
                 uint32_t name,
                 const char *interface,
                 uint32_t version)
{
	struct desktop_shell *twshell = (struct desktop_shell *)data;

	if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stdout, "shell registé\n");
		twshell->interface = (struct taiwins_shell *)
			wl_registry_bind(wl_registry, name,
			                 &taiwins_shell_interface, version);
		taiwins_shell_add_listener(twshell->interface,
		                           &tw_shell_impl, twshell);
	} else if (strcmp(interface, taiwins_theme_interface.name) == 0) {
		fprintf(stdout, "theme registe\n");
		twshell->theme_interface = (struct taiwins_theme *)
			wl_registry_bind(wl_registry, name,
			                 &taiwins_theme_interface, version);
		taiwins_theme_add_listener(twshell->theme_interface,
		                           &tw_theme_impl, twshell);
	} else
		tw_globals_announce(&twshell->globals, wl_registry, name, interface, version);
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
	struct desktop_shell oneshell; //singleton
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
	tw_globals_dispatch_event_queue(&oneshell.globals);
	//clear up
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return 0;
}
