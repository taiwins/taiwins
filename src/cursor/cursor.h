/*
 * Copyright © 2018 xeechou@gmail.com
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef TW_CURSOR_H
#define TW_CURSOR_H

#include <stdint.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_cursor_theme;

struct tw_cursor_image {
	uint32_t width;		/* actual width */
	uint32_t height;	/* actual height */
	uint32_t hotspot_x;	/* hot spot x (must be inside image) */
	uint32_t hotspot_y;	/* hot spot y (must be inside image) */
	uint32_t delay;		/* animation delay to next frame (ms) */
};

struct tw_cursor {
	unsigned int image_count;
	struct tw_cursor_image **images;
	char *name;
};

//struct wl_shm;
struct tw_cursor_theme *
tw_cursor_theme_load(const char *name, int size);

/*
struct tw_cursor_theme *
tw_cursor_theme_load(const char *name, int size, struct wl_shm *shm);
*/

void
tw_cursor_theme_destroy(struct tw_cursor_theme *theme);

struct tw_cursor *
tw_cursor_theme_get_cursor(struct tw_cursor_theme *theme,
			   const char *name);

struct wl_buffer *
tw_cursor_image_get_surface(struct tw_cursor_image *image);

int
tw_cursor_frame(struct tw_cursor *cursor, uint32_t time);

void
tw_cursor_theme_print_cursor_names(const struct tw_cursor_theme *theme);

#ifdef  __cplusplus
}
#endif

#endif
