#include "client.h"

struct wl_buffer_node {
	list_t link;
	struct wl_buffer *wl_buffer;
	struct shm_pool *pool;

	void *addr;
	off_t offset;

	bool inuse;
	int width;
	int height;
	void *userdata;
	void (*release)(void *, struct wl_buffer *);
};


int
shm_pool_init(struct shm_pool *pool, struct wl_shm *shm, size_t size,
	      enum wl_shm_format format)
{
	pool->format = format;
	pool->shm = shm;
	list_init(&pool->wl_buffers);
	if (anonymous_buff_new(&pool->file, size, PROT_READ | PROT_WRITE, MAP_SHARED) < 0) {
		return 0;
	}
	pool->pool = wl_shm_create_pool(shm, pool->file.fd, size);

	return size;
}

void
shm_pool_release(struct shm_pool *pool)
{
	struct wl_buffer_node *v, *n;
	list_for_each_safe(v, n, &pool->wl_buffers, link) {
		wl_buffer_destroy(v->wl_buffer);
		list_remove(&v->link);
		free(v);
	}
	wl_shm_pool_destroy(pool->pool);
	anonymous_buff_close_file(&pool->file);
}


static int
shm_pool_resize(struct shm_pool *pool, off_t newsize)
{
	wl_shm_pool_resize(pool->pool, newsize);
	struct wl_buffer_node *n, *v;
	list_for_each_safe(v, n, &pool->wl_buffers, link) {
		v->addr = (char *)pool->file.addr + v->offset;
	}
	return newsize;
}


bool
shm_pool_buffer_inuse(struct wl_buffer *wl_buffer)
{
	struct wl_buffer_node *node = (struct wl_buffer_node *)
		wl_buffer_get_user_data(wl_buffer);
	return node->inuse;
}

static void
wl_buffer_release_notify(void *data, struct wl_buffer *buffer)
{
//	fprintf(stderr, "wl_buffer %p is released by server\n", buffer);
	struct wl_buffer_node *node = (struct wl_buffer_node *)data;
	if (node->userdata &&  node->release)
		node->release(node->userdata, buffer);
}


static struct wl_buffer_listener buffer_listener = {
	.release = wl_buffer_release_notify
};

void
shm_pool_set_buffer_release_notify(struct wl_buffer *wl_buffer,
				   void (*cb)(void *, struct wl_buffer *),
				   void *data)
{
	struct wl_buffer_node *node = (struct wl_buffer_node *)
		wl_buffer_get_user_data(wl_buffer);
	node->userdata = data;
	node->release = cb;
}

//the solution here should be actually change the width into width * stride
struct wl_buffer *
shm_pool_alloc_buffer(struct shm_pool *pool, size_t width, size_t height)
{
	size_t stride = stride_of_wl_shm_format(pool->format);
	size_t size = stride * height * width;
	{
		struct wl_buffer_node *v, *n;
		list_for_each_safe(v, n, &pool->wl_buffers, link) {
			size_t node_size = v->width * v->height * stride;
			//only return if found the exact size
			if (!v->inuse && node_size == size) {
				v->inuse = true;
				return v->wl_buffer;
			}
		}
	}
	size_t origin_size = pool->file.size;
	off_t offset = anonymous_buff_alloc_by_offset(&pool->file, size);
	if (pool->file.size > origin_size)
		shm_pool_resize(pool, pool->file.size);
	struct wl_buffer *wl_buffer = wl_shm_pool_create_buffer(pool->pool, offset,
								width, height,
								stride * width,
								pool->format);
	struct wl_buffer_node *node_buffer = (struct wl_buffer_node *)
		malloc(sizeof(*node_buffer));
	list_init(&node_buffer->link);
	node_buffer->pool = pool;
	node_buffer->offset = offset;
	node_buffer->wl_buffer = wl_buffer;
	node_buffer->addr = (char *)pool->file.addr + offset;
	node_buffer->width = width;
	node_buffer->height = height;
	node_buffer->inuse = true;
	list_append(&pool->wl_buffers, &node_buffer->link);
	wl_buffer_add_listener(wl_buffer, &buffer_listener, node_buffer);
	return wl_buffer;
}

void
shm_pool_buffer_free(struct wl_buffer *wl_buffer)
{
	struct wl_buffer_node *node = wl_buffer_get_user_data(wl_buffer);
	node->inuse = false;
}

void *
shm_pool_buffer_access(struct wl_buffer *wl_buffer)
{
	struct wl_buffer_node *node = wl_buffer_get_user_data(wl_buffer);
	if (node->inuse == false)
		node->inuse = true;
	return node->addr;
}


size_t
shm_pool_buffer_size(struct wl_buffer *wl_buffer)
{
	struct wl_buffer_node *node = wl_buffer_get_user_data(wl_buffer);
	size_t stride = stride_of_wl_shm_format(node->pool->format);
	return node->width * node->height * stride;
}
