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

#include "xcursor.h"
#include "cursor.h"
//#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <compositor.h>
#include <wayland-server.h>

#include "os-compatibility.h"

struct shm_pool {
	int fd;
	unsigned int size;
	unsigned int used;
	char *data;
};

static struct shm_pool *
shm_pool_create(struct wl_shm *shm, int size)
{
	struct shm_pool *pool;

	pool = (struct shm_pool *)malloc(sizeof *pool);
	if (!pool)
		return NULL;

	pool->fd = os_create_anonymous_file(size);
	if (pool->fd < 0)
		goto err_free;

	pool->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			  pool->fd, 0);

	if (pool->data == MAP_FAILED)
		goto err_close;

//	pool->pool = wl_shm_create_pool(shm, pool->fd, size);
	pool->size = size;
	pool->used = 0;

	return pool;

err_close:
	close(pool->fd);
err_free:
	free(pool);
	return NULL;
}

static int
shm_pool_resize(struct shm_pool *pool, int size)
{
	if (ftruncate(pool->fd, size) < 0)
		return 0;

	munmap(pool->data, pool->size);
	//this is like realloc
	pool->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			  pool->fd, 0);
	pool->size = size;

	return 1;
}

static int
shm_pool_allocate(struct shm_pool *pool, int size)
{
	int offset;

	if (pool->used + size > pool->size)
		if (!shm_pool_resize(pool, 2 * pool->size + size))
			return -1;

	offset = pool->used;
	pool->used += size;

	return offset;
}

static void
shm_pool_destroy(struct shm_pool *pool)
{
	munmap(pool->data, pool->size);
	close(pool->fd);
	free(pool);
}



struct tw_cursor_theme {
	unsigned int cursor_count;
	struct tw_cursor **cursors;
	/* the pool has the data */
	struct shm_pool *pool;
	char *name;
	int size;
};

struct cursor_image {
	struct tw_cursor_image image;
	struct tw_cursor_theme *theme;
	int offset; /* data offset of this image in the shm pool */
};

struct cursor {
	struct tw_cursor cursor;
	uint32_t total_delay; /* length of the animation in ms */
};

/** Get an shm buffer for a cursor image
 *
 * \param image The cursor image
 * \return An shm buffer for the cursor image. The user should not destroy
 * the returned buffer.

struct wl_buffer *
tw_cursor_image_get_surface(struct tw_cursor_image *_img)
{
	struct cursor_image *image = (struct cursor_image *) _img;
	struct tw_cursor_theme *theme = image->theme;

	if (!image->buffer) {
		image->buffer =
			wl_shm_pool_create_buffer(theme->pool->pool,
						  image->offset,
						  _img->width, _img->height,
						  _img->width * 4,
						  WL_SHM_FORMAT_ARGB8888);
	};

	return image->buffer;
}
*/


/** Assign the buffer to the weston_pointer
 *
 * \param pointer The weston pointer
 * \param image The cursor image
 */
void
tw_pointer_assign_cursor_img(struct weston_pointer *pointer, struct tw_cursor_image *_img)
{
	struct cursor_image *image = (struct cursor_image *) _img;
	struct weston_compositor *compositor = pointer->seat->compositor;
	if (!pointer->sprite) {
		struct weston_surface *surface = weston_surface_create(compositor);
		struct weston_view *view = weston_view_create(surface);
		struct weston_renderer *renderer = compositor->renderer;
		pointer->sprite = view;
		weston_buffer *buffer = (struct weston_buffer*) malloc(sizeof(*buffer));
		weston_buffer_reference(&surface->buffer_ref, buffer);
		weston_layer_entry_insert(&compositor->cursor_layer.view_list, &pointer->sprite->layer_link);
	}
	struct weston_surface *surface = pointer->sprite->surface;
	struct weston_buffer *buffer = surface->buffer_ref.buffer;
	//TODO, we need to make a destroy signal for surface this surface
	buffer->legacy_buffer = image->theme->pool->data + image->offset;
	buffer->width = _img->width;
	buffer->height = _img->height;
	//this could be a setup function, but yeah, actually the sprite get
	//destroyed at wl_pointer_set_cursor. Or weston_background does the weston_view_create thing?
}



static void
tw_cursor_image_destroy(struct tw_cursor_image *_img)
{
	struct cursor_image *image = (struct cursor_image *) _img;

	free(image);
}

static void
tw_cursor_destroy(struct tw_cursor *cursor)
{
	unsigned int i;

	for (i = 0; i < cursor->image_count; i++)
		tw_cursor_image_destroy(cursor->images[i]);

	free(cursor->name);
	free(cursor);
}

static struct tw_cursor *
tw_cursor_create_from_xcursor_images(XcursorImages *images,
				     struct tw_cursor_theme *theme)
{
	//we have cursor.cursor which is exposed, cursor->cursor.images is just
	//metadata
	struct cursor *cursor;
	struct cursor_image *image;
	int i, size;

	cursor = (struct cursor *)malloc(sizeof *cursor);
	if (!cursor)
		return NULL;

	cursor->cursor.image_count = images->nimage;
	cursor->cursor.images =
		malloc(images->nimage * sizeof cursor->cursor.images[0]);
	if (!cursor->cursor.images) {
		free(cursor);
		return NULL;
	}

	cursor->cursor.name = strdup(images->name);
	cursor->total_delay = 0;

	for (i = 0; i < images->nimage; i++) {
		image = (struct cursor_image *)malloc(sizeof *image);
		cursor->cursor.images[i] = (struct tw_cursor_image *) image;

		image->theme = theme;

		image->image.width = images->images[i]->width;
		image->image.height = images->images[i]->height;
		image->image.hotspot_x = images->images[i]->xhot;
		image->image.hotspot_y = images->images[i]->yhot;
		image->image.delay = images->images[i]->delay;
		cursor->total_delay += image->image.delay;

		/* copy pixels to shm pool */
		size = image->image.width * image->image.height * 4;
		image->offset = shm_pool_allocate(theme->pool, size);
		memcpy(theme->pool->data + image->offset,
		       images->images[i]->pixels, size);
	}

	return &cursor->cursor;
}

static void
load_callback(XcursorImages *images, void *data)
{
	struct tw_cursor_theme *theme = data;
	struct tw_cursor *cursor;

	if (tw_cursor_theme_get_cursor(theme, images->name)) {
		XcursorImagesDestroy(images);
		return;
	}

	cursor = tw_cursor_create_from_xcursor_images(images, theme);

	if (cursor) {
		theme->cursor_count++;
		theme->cursors =
			realloc(theme->cursors,
				theme->cursor_count * sizeof theme->cursors[0]);

		theme->cursors[theme->cursor_count - 1] = cursor;
	}

	XcursorImagesDestroy(images);
}

/** Load a cursor theme to memory shared with the compositor
 *
 * \param name The name of the cursor theme to load. If %NULL, the default
 * theme will be loaded.
 * \param size Desired size of the cursor images.
 * \param shm The compositor's shm interface.
 *
 * \return An object representing the theme that should be destroyed with
 * tw_cursor_theme_destroy() or %NULL on error.
 */
struct tw_cursor_theme *
tw_cursor_theme_load(const char *name, int size)
{
	struct tw_cursor_theme *theme;

	theme = (struct tw_cursor_theme *)malloc(sizeof *theme);
	if (!theme)
		return NULL;

	if (!name)
		name = "default";

	theme->name = strdup(name);
	theme->size = size;
	theme->cursor_count = 0;
	theme->cursors = NULL;

	xcursor_load_theme(name, size, load_callback, theme);

	return theme;
}

/** Destroys a cursor theme object
 *
 * \param theme The cursor theme to be destroyed
 */
void
tw_cursor_theme_destroy(struct tw_cursor_theme *theme)
{
	unsigned int i;

	for (i = 0; i < theme->cursor_count; i++)
		tw_cursor_destroy(theme->cursors[i]);

	shm_pool_destroy(theme->pool);
	free(theme->cursors);
	free(theme);
}

/** Get the cursor for a given name from a cursor theme
 *
 * \param theme The cursor theme
 * \param name Name of the desired cursor
 * \return The theme's cursor of the given name or %NULL if there is no
 * such cursor
 */
struct tw_cursor *
tw_cursor_theme_get_cursor(struct tw_cursor_theme *theme,
			   const char *name)
{
	unsigned int i;

	for (i = 0; i < theme->cursor_count; i++) {
		if (strcmp(name, theme->cursors[i]->name) == 0)
			return theme->cursors[i];
	}

	return NULL;
}

/** Find the frame for a given elapsed time in a cursor animation
 *
 * \param cursor The cursor
 * \param time Elapsed time since the beginning of the animation
 *
 * \return The index of the image that should be displayed for the
 * given time in the cursor animation.
 */
int
tw_cursor_frame(struct tw_cursor *_cursor, uint32_t time)
{
	struct cursor *cursor = (struct cursor *) _cursor;
	uint32_t t;
	int i;

	if (cursor->cursor.image_count == 1)
		return 0;

	i = 0;
	t = time % cursor->total_delay;

	while (t - cursor->cursor.images[i]->delay < t)
		t -= cursor->cursor.images[i++]->delay;

	return i;
}

/**
 * for the debugging purpose, I am keeping this function
 */
void
tw_cursor_theme_print_cursor_names(const struct tw_cursor_theme *theme)
{
	unsigned int i;
	for (i = 0; i < theme->cursor_count; i++) {
		fprintf(stderr, "cursor names: %s\n", theme->cursors[i]->name);
	}
}
