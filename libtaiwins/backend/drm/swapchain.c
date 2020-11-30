/*
 * swapchain.c - taiwins server drm swapchain functions
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
#include <string.h>
#include <wayland-util.h>
#include <taiwins/objects/utils.h>

#include "internal.h"

void
tw_drm_swapchain_init(struct tw_drm_swapchain *sc, unsigned int cnt)
{
	wl_list_init(&sc->fbs);
	sc->cnt = cnt;
	for (unsigned i = 0; i < cnt; i++) {
		sc->imgs[i].fb = 0;
		sc->imgs[i].locked = false;
		sc->imgs[i].handle = (uintptr_t)NULL;
		wl_list_init(&sc->imgs[i].link);
		wl_list_insert(sc->fbs.next, &sc->imgs[i].link);
	}
}

void
tw_drm_swapchain_push(struct tw_drm_swapchain *sc, struct tw_drm_fb *fb)
{
	//ensure fb is inside sc
	assert(fb == &sc->imgs[0] || fb == &sc->imgs[1] || fb == &sc->imgs[2]);
	//ensure fb is free
	assert((&fb->link == fb->link.prev) && (&fb->link == fb->link.next));
	wl_list_insert(&sc->fbs, &fb->link);
}

struct tw_drm_fb *
tw_drm_swapchain_pop(struct tw_drm_swapchain *sc)
{
	struct tw_drm_fb *fb = NULL;

	if (wl_list_length(&sc->fbs) == 0) {
		return NULL;
	} else {
		fb = wl_container_of(sc->fbs.prev, fb, link);
		wl_list_remove(&fb->link);
		wl_list_init(&fb->link);
		return fb;
	}
}

void
tw_drm_swapchain_fini(struct tw_drm_swapchain *sc)
{
	memset(sc, 0, sizeof(*sc));
}
