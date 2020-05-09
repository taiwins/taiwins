/*
 * shell_panel.c - taiwins client shell panel implementation
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

#include "event_queue.h"
#include "helpers.h"
#include "shell.h"


static int
panel_launch_widget(struct tw_event *event, int fd)
{
	shell_launch_widget(event->data);
	return  TW_EVENT_DEL;
}

static inline struct nk_vec2
widget_launch_point_flat(struct nk_vec2 *label_span, struct shell_widget *clicked,
			 struct tw_appsurf *panel_surf)
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
	if (shell_output->shell->panel_pos == TAIWINS_SHELL_PANEL_POS_TOP)
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
			    struct tw_appsurf *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	nk_text_width_f text_width = ctx->style.font->width;
	struct shell_widget_label widget_label;
	struct shell_widget *widget = NULL;
	//use the icon font
	nk_style_push_font(ctx, shell->icon_font);

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
	nk_style_pop_font(ctx);
}

static void
shell_panel_frame(struct nk_context *ctx, float width, float height,
		  struct tw_appsurf *panel_surf)
{
	struct shell_output *shell_output =
		container_of(panel_surf, struct shell_output, panel);
	struct desktop_shell *shell = shell_output->shell;
	struct shell_widget_label widget_label;
	nk_text_width_f text_width = ctx->style.font->width;
	//drawing labels
	size_t n_widgets = wl_list_length(&shell->shell_widgets);
	struct shell_widget *widget = NULL, *clicked = NULL;
	struct nk_vec2 label_span = nk_vec2(0, 0);
	struct nk_style_button *style = &ctx->style.contextual_button;

	int h = panel_surf->allocation.h;
	int w = panel_surf->allocation.w;
	//use the icon font
	nk_style_push_font(ctx, shell->icon_font);
	//draw
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
		nk_button_text_styled(ctx, style, widget_label.label, len);
	}
	nk_layout_row_end(ctx);
	nk_style_pop_font(ctx);

	//widget launch condition, clicked->proxy means widget already clicked
	if (!clicked || clicked->proxy ||
	    !clicked->draw_cb ||
	    shell->transient.wl_surface) //if other surface is ocuppying
		return;

	//launch widget
	struct widget_launch_info *info = &shell->widget_launch;
	struct tw_event launch_widget = {
		.data = shell,
		.cb = panel_launch_widget,
	};

	info->widget = clicked;
	info->output = container_of(panel_surf, struct shell_output, panel);
	struct nk_vec2 p = widget_launch_point_flat(&label_span, clicked, panel_surf);
	info->x = (int)p.x;
	info->y = (int)p.y;

	tw_event_queue_add_idle(&shell->globals.event_queue, &launch_widget);
}

void
shell_init_panel_for_output(struct shell_output *w)
{
	struct desktop_shell *shell = w->shell;
	struct wl_surface *pn_sf;

	//at this point, we are  sure to create the resource
	pn_sf = wl_compositor_create_surface(shell->globals.compositor);
	w->pn_ui = taiwins_shell_create_panel(shell->interface, pn_sf, w->index);
	tw_appsurf_init(&w->panel, pn_sf, &shell->globals,
			 TW_APPSURF_PANEL, TW_APPSURF_NORESIZABLE);
	nk_cairo_impl_app_surface(&w->panel, shell->panel_backend,
				  shell_panel_frame,
				  tw_make_bbox_origin(w->bbox.w,
				                      shell->panel_height,
				                      w->bbox.s));

	struct shell_widget *widget;
	wl_list_for_each(widget, &shell->shell_widgets, link) {
		shell_widget_hook_panel(widget, &w->panel);
		shell_widget_activate(widget, &shell->globals.event_queue);
	}

	nk_wl_test_draw(shell->panel_backend, &w->panel,
			shell_panel_measure_leading);
}

void
shell_resize_panel_for_output(struct shell_output *w)
{
	nk_wl_test_draw(w->shell->panel_backend, &w->panel,
			shell_panel_measure_leading);

	w->panel.flags &= ~TW_APPSURF_NORESIZABLE;
	tw_appsurf_resize(&w->panel,
	                  w->bbox.w,
	                  w->shell->panel_height,
	                  w->bbox.s);
	w->panel.flags |= TW_APPSURF_NORESIZABLE;
}
