/*
 * cursor.c - taiwins xwayland cursor library
 *
 * Copyright (c) 2021 Xichen Zhou
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

#include <X11/X.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <X11/Xcursor/Xcursor.h>
#include <ctypes/helpers.h>

#include "internal.h"

/*
 * The following correspondences between file names and cursors was copied
 * from: https://bugs.kde.org/attachment.cgi?id=67313
 */

static const char *tops[] = {
	"top_side",
	"n-resize",
	"size_ver"
};

static const char *bottoms[] = {
	"bottom_side",
	"s-resize",
	"size_ver"
};

static const char *lefts[] = {
	"left_side",
	"w-resize",
	"size_hor"
};

static const char *rights[] = {
	"right_side",
	"e-resize",
	"size_hor"
};

static const char *top_lefts[] = {
	"top_left_corner",
	"nw-resize",
	"size_fdiag"
};

static const char *top_rights[] = {
	"top_right_corner",
	"ne-resize",
	"size_bdiag"
};

static const char *bottom_lefts[] = {
	"bottom_left_corner",
	"sw-resize",
	"size_bdiag"
};

static const char *bottom_rights[] = {
	"bottom_right_corner",
	"se-resize",
	"size_fdiag"
};

static const char *left_ptrs[] = {
	"left_ptr",
	"default",
	"top_left_arrow",
	"left-arrow"
};

struct xcursor_options {
	const char **names;
	size_t count;
};

static const struct xcursor_options cursors_options[TW_XCURSOR_LEFT_PTR+1] = {
	[TW_XCURSOR_TOP] =  {tops, NUMOF(tops)},
	[TW_XCURSOR_BOTTOM] =  {bottoms, NUMOF(bottoms)},
	[TW_XCURSOR_LEFT] = {lefts, NUMOF(lefts)},
	[TW_XCURSOR_RIGHT] = {rights, NUMOF(rights)},
	[TW_XCURSOR_TOP_LEFT] = {top_lefts, NUMOF(top_lefts)},
	[TW_XCURSOR_TOP_RIGHT] = {top_rights, NUMOF(top_rights)},
	[TW_XCURSOR_BOTTOM_LEFT] = {bottom_lefts, NUMOF(bottom_lefts)},
	[TW_XCURSOR_BOTTOM_RIGHT] = {bottom_rights, NUMOF(bottom_rights)},
	[TW_XCURSOR_LEFT_PTR] = {left_ptrs, NUMOF(left_ptrs)},
};


static xcb_cursor_t
cursor_image_load_cursor(struct tw_xwm *xwm, const XcursorImage *img)
{
	xcb_connection_t *c = xwm->xcb_conn;
	xcb_screen_iterator_t s = xcb_setup_roots_iterator(xcb_get_setup(c));
	xcb_screen_t *screen = s.data;
	xcb_gcontext_t gc;
	xcb_pixmap_t pix;
	xcb_render_picture_t pic;
	xcb_cursor_t cursor;
	int stride = img->width * 4;

	pix = xcb_generate_id(c);
	xcb_create_pixmap(c, 32, pix, screen->root, img->width, img->height);

	pic = xcb_generate_id(c);
	xcb_render_create_picture(c, pic, pix, xwm->format_rgba, 0, 0);

	gc = xcb_generate_id(c);
	xcb_create_gc(c, gc, pix, 0, 0);

	xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, pix, gc,
		      img->width, img->height, 0, 0, 0, 32,
		      stride * img->height, (uint8_t *) img->pixels);
	xcb_free_gc(c, gc);

	cursor = xcb_generate_id(c);
	xcb_render_create_cursor(c, cursor, pic, img->xhot, img->yhot);

	xcb_render_free_picture(c, pic);
	xcb_free_pixmap(c, pix);

	return cursor;
}

static xcb_cursor_t
cursor_images_load_cursor(struct tw_xwm *xwm, const XcursorImages *images)
{
	//TODO: load animated cursors as well
	if (images->nimage != 1)
		return XCB_CURSOR_NONE;

	return cursor_image_load_cursor(xwm, images->images[0]);
}

xcb_cursor_t
cursor_load_file(struct tw_xwm *xwm, const char *file)
{
	xcb_cursor_t cursor;
	XcursorImages *images;
	char *v = NULL;
	int size = 0;

	if (!file)
		return 0;

	if ((v = getenv ("XCURSOR_SIZE")))
		size = atoi(v);
	if (!size)
		size = 32;
	images = XcursorLibraryLoadImages (file, NULL, size);
	cursor = cursor_images_load_cursor(xwm, images);
	XcursorImagesDestroy (images);

	return cursor;
}

xcb_cursor_t
tw_xcursor_load(struct tw_xwm *xwm, enum tw_xcursor_type type)
{
	const char *name;
	xcb_cursor_t cursor = XCB_CURSOR_NONE;

	for (unsigned i = 0; i < cursors_options[type].count; i++) {
		name = cursors_options[type].names[i];
		cursor = cursor_load_file(xwm, name);
		if (cursor != XCB_CURSOR_NONE)
			break;
	}
	return cursor;
}

void
tw_xcursor_set(struct tw_xwm *xwm, enum tw_xcursor_type type, xcb_window_t win)
{
	xcb_cursor_t cursor = xwm->cursors[type];

	if (xwm->cursor_curr == cursor)
		return;
	xwm->cursor_curr = cursor;

	xcb_change_window_attributes(xwm->xcb_conn, win, XCB_CW_CURSOR, &cursor);
	xcb_flush(xwm->xcb_conn);
}
