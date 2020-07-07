/*
 * shell_bg.c - taiwins client shell background implementation
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

#include <twclient/image_cache.h>
#include <ctypes/strops.h>
#include "shell.h"

static bool
shell_background_start_menu(struct tw_appsurf *surf, const struct tw_app_event *e)
{
	struct shell_output *output = container_of(surf, struct shell_output, background);
	struct desktop_shell *shell = output->shell;

	if (e->ptr.btn == BTN_RIGHT && e->ptr.state)
		shell_launch_menu(shell, output, e->ptr.x, e->ptr.y);
	return true;
}

static void
shell_background_frame(struct tw_appsurf *surf, struct wl_buffer *buffer,
		       struct tw_bbox *geo)

{
	//now it respond only tw_appsurf_frame, we only need to add idle task
	//as for tw_appsurf_frame later
	struct shell_output *output = container_of(surf, struct shell_output, background);
	struct desktop_shell *shell = output->shell;
	void *buffer_data = tw_shm_pool_buffer_access(buffer);
	char *wallpaper_path = (shell->config.request_wallpaper) ?
		shell->config.request_wallpaper(&shell->config) : NULL;
	*geo = surf->allocation;
	if (!image_load_for_buffer(wallpaper_path, surf->pool->format,
		       surf->allocation.w*surf->allocation.s,
		       surf->allocation.h*surf->allocation.s,
		       (unsigned char *)buffer_data)) {
		/* shell_notif_msg(shell, 128, "failed to load image %s\n", */
		/*                 wallpaper_path); */
	}
	if (wallpaper_path) free(wallpaper_path);
}

static void
shell_background_impl_filter(struct wl_list *head,
			     struct tw_app_event_filter *filter)
{
	wl_list_init(&filter->link);
	filter->type = TW_POINTER_BTN;
	filter->intercept = shell_background_start_menu;
	wl_list_insert(head, &filter->link);
}


void
shell_init_bg_for_output(struct shell_output *w)
{
	struct wl_surface *bg_sf;
	struct desktop_shell *shell = w->shell;

	if (w->bg_ui)
		return;
	//background
	bg_sf = wl_compositor_create_surface(shell->globals.compositor);
	w->bg_ui =
		taiwins_shell_create_background(shell->interface, bg_sf, w->index);

	tw_appsurf_init(&w->background, bg_sf,
			 &shell->globals, TW_APPSURF_BACKGROUND,
			 TW_APPSURF_NORESIZABLE);
	shm_buffer_impl_app_surface(&w->background,
				    shell_background_frame,
				    w->bbox);
	shell_background_impl_filter(&w->background.filter_head,
				     &w->background_events);
}

void
shell_resize_bg_for_output(struct shell_output *w)
{
	//TODO hacks here, we temporarily turn off non resizable flags
	w->background.flags &= ~TW_APPSURF_NORESIZABLE;
	tw_appsurf_resize(&w->background, w->bbox.w, w->bbox.h, w->bbox.s);
	w->background.flags |= TW_APPSURF_NORESIZABLE;
}

void
shell_load_wallpaper(struct desktop_shell *shell, const char *path)
{
	if (is_file_exist(path))
		strop_ncpy(shell->wallpaper_path, path, 128);
	if (desktop_shell_n_outputs(shell) == 0)
		return;
	for (int i = 0; i < desktop_shell_n_outputs(shell); i++) {
		struct tw_appsurf *bg =
			&shell->shell_outputs[i].background;
		if (bg->wl_surface)
			tw_appsurf_frame(bg, false);
	}
}
