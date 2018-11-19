#include <assert.h>
#include "client.h"
#include "ui.h"
#include "egl.h"



void
appsurface_init(struct app_surface *surf, struct app_surface *p,
		enum APP_SURFACE_TYPE type, struct wl_surface *wl_surface,
		struct wl_proxy *protocol)
{
	surf->type = type;
	surf->parent = p;
	surf->wl_surface = wl_surface;
	surf->protocol = protocol;
	wl_surface_set_user_data(wl_surface, surf);
}


static void
appsurface_destroy_with_egl(struct app_surface *surf)
{
	eglDestroySurface(surf->egldisplay, surf->eglsurface);
	wl_egl_window_destroy(surf->eglwin);
	surf->egldisplay = EGL_NO_DISPLAY;
//	eglMakeCurrent(surf->egldisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	wl_surface_destroy(surf->wl_surface);
	wl_proxy_destroy(surf->protocol);
	surf->eglwin = NULL;
	surf->eglsurface = EGL_NO_SURFACE;
	surf->destroy = NULL;
}


static void
appsurface_destroy_with_buffer(struct app_surface *surf)
{
	//buffer is not managed here, it has released signal, so we don't need
	//to worry about it.
	for (int i = 0; i < 2; i++) {
		//the wl_buffer itself is destroyed at shm_pool destructor
		if (surf->wl_buffer[i]) {
			shm_pool_buffer_free(surf->wl_buffer[i]);
			surf->committed[i] = false;
			surf->dirty[i] = false;
			surf->wl_buffer[i] = NULL;
		}
	}
	wl_surface_destroy(surf->wl_surface);
	wl_proxy_destroy(surf->protocol);
	surf->destroy = NULL;
}

void
appsurface_init_buffer(struct app_surface *surf, struct shm_pool *shm,
		       const struct bbox *bbox)
{
	surf->px = bbox->x; surf->py = bbox->y;
	surf->w  = bbox->w; surf->h  = bbox->h;
	size_t stride = stride_of_wl_shm_format(shm->format);
	surf->pool = shm;

	for (int i = 0; i < 2; i++) {
		surf->wl_buffer[i] = shm_pool_alloc_buffer(shm, surf->w * stride, surf->h);
		surf->dirty[i] = false;
		surf->committed[i] = false;
		shm_pool_wl_buffer_set_release(surf->wl_buffer[i], appsurface_buffer_release, surf);
	}
	surf->destroy = appsurface_destroy_with_buffer;
}

void
app_surface_init_egl(struct app_surface *surf, struct egl_env *env)
{
	surf->egldisplay = env->egl_display;
	surf->eglwin = wl_egl_window_create(surf->wl_surface,
					    surf->w * surf->s, surf->h * surf->s);
	surf->eglsurface =
		eglCreateWindowSurface(env->egl_display,
				       env->config,
				       (EGLNativeWindowType)surf->eglwin,
				       NULL);

	surf->destroy = appsurface_destroy_with_egl;
	assert(surf->eglsurface);
	assert(surf->eglwin);

}

void
appsurface_init_input(struct app_surface *surf, keycb_t keycb, pointron_t pointron,
		      pointrbtn_t pointrbtn, pointraxis_t pointraxis)
{
	surf->keycb = keycb;
	surf->pointron = pointron;
	surf->pointrbtn = pointrbtn;
	surf->pointraxis = pointraxis;
}



static void
app_surface_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
	if (wl_callback)
		wl_callback_destroy(wl_callback);
}


static struct wl_callback_listener app_surface_done_listener = {
	.done = app_surface_done
};

static void
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
	wl_surface_damage(surf->wl_surface, 0, 0, surf->w, surf->h);
	wl_surface_commit(surf->wl_surface);
	surf->committed[1] = true;
	//this way we should guarentee that all the committed surface is clean now.
	surf->dirty[1] = false;
	//if b1 is not free, then we have no change issues.
	appsurface_swap_dbuffer(surf);
}

void
appsurface_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct app_surface *appsurf = (struct app_surface *)data;
//	fprintf(stderr, "the type of the surface is %s\n",
//		(appsurf->type == APP_BACKGROUND) ? "background" : "panel");

	if (wl_buffer == appsurf->wl_buffer[0]) {
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


void
app_surface_init(struct app_surface *surf, struct wl_surface *wl_surface,
		 struct wl_proxy *proxy)
{
	*surf = (struct app_surface){0};
	surf->wl_surface = wl_surface;
	surf->protocol = proxy;
	wl_surface_set_user_data(wl_surface, surf);
}



static void
app_surface_frame_done(void *user_data, struct wl_callback *cb, uint32_t data)
{
	if (cb)
		wl_callback_destroy(cb);
	struct app_surface *surf = (struct app_surface *)user_data;
	surf->do_frame(surf, data);
}

static struct wl_callback_listener app_surface_wl_frame_impl = {
	.done = app_surface_frame_done,
};

void
app_surface_request_frame(struct app_surface *surf)
{
	struct wl_callback *callback = wl_surface_frame(surf->wl_surface);
	wl_callback_add_listener(callback, &app_surface_wl_frame_impl, surf);
}

/*
 * Here we implement a sample attach -> damage -> commit routine for the
 * wl_buffer. It should be straightforward.
 *
 * I should expect a free surface to draw.
 */
static void
shm_buffer_surface_swap(struct app_surface *surf)
{
	struct wl_buffer *free_buffer = NULL;
	bool *dirty = NULL; bool *committed = NULL;
	int32_t x, y, w, h;
	shm_buffer_draw_t draw_cb = surf->user_data;

	for (int i = 0; i < 2; i++) {
		if (surf->committed[i] || surf->dirty[i])
			continue;
		free_buffer = surf->wl_buffer[i];
		dirty = &surf->dirty[i];
		committed = &surf->committed[i];
		break;
	}
	if (!free_buffer) //I should never be here, should I stop in this function?
		return;
	//also, we should have frame callback here.
	if (surf->need_animation)
		app_surface_request_frame(surf);
	wl_surface_attach(surf->wl_surface, free_buffer, 0, 0);
	draw_cb(surf, free_buffer, &x, &y, &w, &h);
	wl_surface_damage(surf->wl_surface, x, y, w, h);
	//if output has transform, we need to add it here as well.
	//wl_surface_set_buffer_transform && wl_surface_set_buffer_scale
	wl_surface_commit(surf->wl_surface);
}

static void
shm_buffer_destroy_app_surface(struct app_surface *surf)
{
	for (int i = 0; i < 2; i++) {
		shm_pool_buffer_free(surf->wl_buffer[i]);
		surf->dirty[i] = false;
		surf->committed[i] = false;
	}
	surf->pool = NULL;
	surf->user_data = NULL;
}

static void
shm_wl_buffer_release(void *data,
		      struct wl_buffer *wl_buffer)
{
	struct app_surface *surf = (struct app_surface *)data;
	for (int i = 0; i < 2; i++)
		if (surf->wl_buffer[i] == wl_buffer) {
			surf->dirty[i] = false;
			surf->committed[i] = false;
			break;
		}
}

static struct wl_buffer_listener shm_wl_buffer_impl = {
	.release = shm_wl_buffer_release,
};


void
shm_buffer_impl_app_surface(struct app_surface *surf, struct shm_pool *pool,
			    shm_buffer_draw_t draw_call, uint32_t w, uint32_t h)
{
	surf->do_frame = shm_buffer_surface_swap;
	surf->user_data = draw_call;
	surf->destroy = shm_buffer_destroy_app_surface;
	surf->pool = pool;
	surf->w = w; surf->h = h; surf->s = 1;
	surf->px = 0; surf->py = 0;
	for (int i = 0; i < 2; i++) {
		surf->wl_buffer[i] = shm_pool_alloc_buffer(pool, w, h);
		surf->dirty[i] = false;
		surf->committed[i] = false;
		wl_buffer_add_listener(surf->wl_buffer[i],
				       &shm_wl_buffer_impl, surf);
	}
	//TODO we should be able to resize the surface as well.
}


static void
embeded_app_surface_do_frame(struct app_surface *surf, uint32_t data)
{
	surf->parent->do_frame(surf->parent, data);
}

void
embeded_impl_app_surface(struct app_surface *surf, struct app_surface *parent,
			 uint32_t w, uint32_t h, uint32_t px, uint32_t py)
{
	surf->wl_surface = NULL;
	surf->protocol = NULL;
	surf->parent = parent;
	surf->do_frame = embeded_app_surface_do_frame;
	surf->w = w; surf->h = h; surf->s = 1;
	surf->px = px; surf->py = py;
}
