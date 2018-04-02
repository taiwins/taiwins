#include "client.h"
#include "ui.h"


void
appsurface_init(struct app_surface *appsurf, struct app_surface *parent,
		enum APP_SURFACE_TYPE type, struct wl_compositor *compositor,
		struct wl_output *output)
{

	//okay, first thing to do
	*appsurf = (struct app_surface){0};

	appsurf->type = type;
	appsurf->parent = parent;
	appsurf->wl_output = output;

	//create its actual surface
	appsurf->wl_surface = wl_compositor_create_surface(compositor);
	wl_surface_set_user_data(appsurf->wl_surface, appsurf);
	//the anchor and size is also undefined
}

void
appsurface_init_buffer(struct app_surface *surf, struct shm_pool *shm,
		       const struct bbox *bbox)
{
	surf->px = bbox->x; surf->py = bbox->y;
	surf->w  = bbox->w; surf->h  = bbox->h;
	surf->pool = shm;

	for (int i = 0; i < 2; i++) {
		surf->wl_buffer[i] = shm_pool_alloc_buffer(shm, surf->w, surf->h);
		surf->dirty[i] = false;
		surf->committed[i] = false;
	}
}

void
appsurface_initfor_subapps(struct app_surface *surf,
			   struct app_surface *(*find_subapp)(int, int),
			   int (*n_subapp)(void),
			   int (*paint)(struct app_surface *,
					const struct bbox *, const void *,
					enum wl_shm_format),
			   struct app_surface *(*add)(struct app_surface *))
{
	surf->find_subapp_at_xy = find_subapp;
	surf->n_subapp = n_subapp;
	surf->paint_subsurface = paint;
	surf->add_subapp = add;
}


static bool COMMITTED = false;
static void
app_surface_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
	COMMITTED = false;
//	struct app_surface *appsurf = (struct app_surface *)data;
	if (wl_callback)
		wl_callback_destroy(wl_callback);
}


static struct wl_callback_listener app_surface_done_listener = {
	.done = app_surface_done
};

void
appsurface_swap_dbuffer(struct app_surface *surf)
{
	struct wl_buffer *tmp = surf->wl_buffer[0];
	bool tmpcommit  = surf->committed[0];
	bool tmpdirty = surf->dirty[0];

	surf->wl_buffer[0] = surf->wl_buffer[1];
	surf->committed[0] = surf->committed[1];
	surf->dirty[0] = surf->dirty[1];

	surf->wl_buffer[1] = tmp;
	surf->committed[1] = tmpcommit;
	surf->dirty[1] = tmpdirty;
}

//it must have at least one buffer libre.
void
appsurface_fadc(struct app_surface *surf)
{
	//if b2 is not free, we shouldn't do anything.
	if(surf->committed[1] || !surf->dirty[1]) {
		return;
	}
	wl_surface_attach(surf->wl_surface, surf->wl_buffer[1], 0, 0);
	struct wl_callback *callback = wl_surface_frame(surf->wl_surface);
	wl_callback_add_listener(callback, &app_surface_done_listener, surf);
	wl_surface_damage(surf->wl_surface, 900, surf->py, 100, surf->h);
	wl_surface_commit(surf->wl_surface);
	surf->committed[1] = true;
	//this way we should guarentee that all the committed surface is clean now.
	surf->dirty[1] = false;
	//if b1 is not free, then we have no change issues.
	appsurface_swap_dbuffer(surf);
	COMMITTED = true;
//	if (!surf->committed[0])
//		appsurface_swap_dbuffer(surf);

}

void appsurface_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	fprintf(stderr, "buffer %p  released.\n", wl_buffer);
	struct app_surface *appsurf = (struct app_surface *)data;

	if (wl_buffer == appsurf->wl_buffer[0]) {
		if (COMMITTED)
//			fprintf(stderr, "we have framebuffer released before done\n");
		appsurf->committed[0] = false;
		if (appsurf->committed[1])
			appsurface_swap_dbuffer(appsurf);
	}
	else if (wl_buffer == appsurf->wl_buffer[1]) {
		appsurf->committed[1] = false;
//		appsurf->dirty[1] = false;
	}
	//other cases:
	//0) b1 free and b2 free. You don't need to do anything
	//1) b2 free and b1 not. nothing to do
	//2) b2 free and b1 free. nothing to do
}
