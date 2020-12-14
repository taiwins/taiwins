/*
 * render_swi.c - taiwins render swapchain context
 *
 * Copyright (c) 2020 Xichen Zhou
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

#include <assert.h>
#include <stdint.h>
#include <taiwins/render_wsi.h>
#include <taiwins/objects/utils.h>
#include <wayland-server.h>
#include <wayland-util.h>

bool
tw_render_wsi_init(struct tw_render_wsi *wsi, enum tw_render_wsi_type type,
                   unsigned cnt, struct tw_render_allocator *allocator)
{
	if (cnt > TW_MAX_SWAP_IMGS || !allocator || !allocator->impl)
		return false;

	wsi->cnt = cnt;
	wsi->type = type;
	wsi->front = NULL;
	wsi->curr = NULL;
	wsi->allocator = allocator;
	wl_list_init(&wsi->back);
	wl_list_init(&wsi->free);

	for (unsigned i = 0; i < cnt; i++) {
		wsi->imgs[i].handle = (uintptr_t)NULL;
		wl_list_init(&wsi->imgs[i].link);
		wl_list_insert(wsi->free.prev, &wsi->imgs[i].link);
	}
	return false;
}

//aquire a free image in the swapchain, set it to the current buffer.
bool
tw_render_wsi_aquire_img(struct tw_render_wsi *wsi)
{
	struct tw_render_swap_img *curr = NULL;

	if (wsi->curr)
		return wsi->curr;
	else if (wl_list_empty(&wsi->free))
		return false;

	curr = wl_container_of(wsi->free.prev, curr, link);
	wl_list_remove(&curr->link);
	wsi->curr = curr;
	return true;
}

//push current into back buffer.
void
tw_render_wsi_push(struct tw_render_wsi *wsi)
{
	struct tw_render_swap_img *img = wsi->curr;

	assert(img == &wsi->imgs[0] || img == &wsi->imgs[1] ||
	       img == &wsi->imgs[2]);
	assert(img->link.next == &img->link && img->link.prev == &img->link);
	//for MAILBOX mode, we should remove the head
	if (wsi->type == TW_RENDER_WSI_MAILBOX && !wl_list_empty(&wsi->back)) {
		struct wl_list *back = wsi->back.next;
		wl_list_remove(back);
		wl_list_insert(wsi->free.prev, back);
	}
	//replacing the back buffer in the MAILBOX mode or insert it to the
	//back for FIFO
	wl_list_insert((wsi->type == TW_RENDER_WSI_MAILBOX) ?
	               &wsi->back : wsi->back.prev, &img->link);
	wsi->curr = NULL;
}

/* should be used by drm_backend on page-flip. */
void
tw_render_wsi_swap_front(struct tw_render_wsi *wsi)
{
	struct tw_render_swap_img *img = wsi->front;

	if (img) {
		wl_list_init(&img->link);
		wl_list_insert(wsi->free.prev, &img->link);
	}
	//at the same time, we should have one img in the back buffer
	if (!wl_list_empty(&wsi->back)) {
		struct wl_list *back = wsi->back.next;
		wl_list_remove(back);
		wsi->front = wl_container_of(back, img, link);
	}
}

void
tw_render_allocator_init(struct tw_render_allocator *allocator,
                         const struct tw_render_allocator_impl *impl);
void
tw_render_allocator_fini(struct tw_render_allocator *allocator);
