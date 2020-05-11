/*
 * shell_ui.c - taiwins client shell ui management
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

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <wayland-client-protocol.h>

#include <ctypes/helpers.h>
#include <ctypes/vector.h>
#include <twclient/nk_backends.h>
#include <twclient/ui.h>
#include <widget/widget.h>

#include "shell.h"

/*******************************************************************************
 * menu widget
 ******************************************************************************/

static void
shell_draw_submenu(struct nk_context *ctx, struct tw_menu_item *item, int id)
{
	struct nk_style_button *style =
		&ctx->style.contextual_button;

	if (nk_tree_push_id(ctx, NK_TREE_TAB, item->endnode.title,
	                    NK_MINIMIZED, id)) {
		for (unsigned i = 0; i < item->len; i++) {
			struct tw_menu_item *sub_item = item + i + 1;
			nk_layout_row_dynamic(ctx, 25, 1);
			if (nk_button_label_styled(ctx, style,
			                           sub_item->endnode.title))
				fprintf(stderr, "should run command %s",
				        item->endnode.cmd);
		}
		nk_tree_pop(ctx);
	}
}

static void
shell_draw_menu(struct nk_context *ctx, float width, float height,
                struct tw_appsurf *app)
{
	static int clicked;
	int i = 0;
	struct tw_menu_item *item;
	struct nk_style_button *style;
	struct shell_widget *menu = container_of(app, struct shell_widget,
	                                         widget);
	vector_t *menu_data = menu->user_data;

	style = &ctx->style.contextual_button;

	while (i < menu_data->len) {
		item = vector_at(menu_data, i);
		if (item->has_submenu)
			shell_draw_submenu(ctx, item, i);
		else {
			nk_layout_row_dynamic(ctx, 25, 1);
			if ((clicked = nk_button_label_styled(ctx, style,
		                                           item->endnode.title)))
				fprintf(stderr, "should run cmd: %s\n",
				        item->endnode.cmd);
		}

		i+= (item->has_submenu) ? 1 + item->len : 1;
	};
}

static struct shell_widget menu_widget = {
	.w = 120,
	.h = 110,
	.draw_cb = shell_draw_menu,
};


/*******************************************************************************
 * notification widget
 ******************************************************************************/

static void
shell_draw_notif(struct nk_context *ctx, float width, float height,
                  struct tw_appsurf *app)
{
	struct shell_widget *notif_widget =
		container_of(app, struct shell_widget, widget);
	struct shell_notif *notif =
		notif_widget->user_data;

	if (!notif)
		return;
	nk_layout_row_dynamic(ctx, height, 1);
	nk_label_wrap(ctx, notif->msg);
}

struct shell_widget notification_widget = {
	.w = 200,
	.h = 40,
	.draw_cb = shell_draw_notif,
};

/*******************************************************************************
 *  shell widget api
 ******************************************************************************/

static void
widget_should_close(void *data, struct taiwins_ui *ui_elem)
{
	struct widget_launch_info *info = (struct widget_launch_info *)data;
	struct shell_widget *widget = info->current;

	taiwins_ui_destroy(widget->proxy);
	tw_appsurf_release(&widget->widget);
	widget->proxy = NULL;
	info->current = NULL;
}

static struct  taiwins_ui_listener widget_impl = {
	.close = widget_should_close,
};

void
shell_close_widget(struct desktop_shell *shell)
{
	taiwins_ui_destroy(shell->widget_launch.current->proxy);
	shell->widget_launch.current->proxy = NULL;
	tw_appsurf_release(&shell->widget_launch.current->widget);
	shell->widget_launch.current = NULL;
}

void
shell_launch_widget(struct desktop_shell *shell)
{
	struct widget_launch_info *info = &shell->widget_launch;
	struct shell_output *shell_output = shell->widget_launch.output;

	if (info->current == info->widget && info->current != NULL)
		return;
	else if (info->current != NULL)
		shell_close_widget(shell);

	struct wl_surface *widget_surface =
		wl_compositor_create_surface(shell->globals.compositor);
	struct taiwins_ui *widget_proxy =
		taiwins_shell_launch_widget(shell->interface, widget_surface,
				       shell_output->index, info->x, info->y);
	info->widget->proxy = widget_proxy;
	taiwins_ui_add_listener(widget_proxy, &widget_impl, info);
	tw_appsurf_init(&info->widget->widget, widget_surface,
	                &shell->globals, TW_APPSURF_WIDGET,
	                TW_APPSURF_NORESIZABLE);
	nk_cairo_impl_app_surface(&info->widget->widget, shell->widget_backend,
	                          info->widget->draw_cb,
	                          tw_make_bbox(info->x, info->y,
	                                       info->widget->w,
	                                       info->widget->h,
	                                       shell_output->bbox.s));

	tw_appsurf_frame(&info->widget->widget, false);
	info->current = info->widget;

}

void
shell_launch_menu(struct desktop_shell *shell, struct shell_output *output,
                  uint32_t x, uint32_t y)
{
	struct widget_launch_info *info = &shell->widget_launch;

	if (info->current)
		shell_close_widget(shell);

	info->x = x;
	info->y = y;
	info->widget = &menu_widget;
	info->output = output;
	info->widget->user_data = &shell->menu;

	shell_launch_widget(shell);
}

void
shell_launch_notif(struct desktop_shell *shell, struct shell_notif *notif)
{
	struct widget_launch_info *info = &shell->widget_launch;
	struct shell_output *output = shell->main_output;
	struct shell_widget *widget = &notification_widget;

	//notification does not race the widget apps
	if (info->current)
		return;

	//hard coded location, we can actually have a location
	info->x = output->bbox.w - widget->w;
	info->y = shell->panel_height + 10;

	info->output = output;
	info->widget = widget;
	widget->user_data = notif;

	shell_launch_widget(shell);
}
