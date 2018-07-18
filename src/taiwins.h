#ifndef TAIWINS_H
#define TAIWINS_H

#include <wayland-server.h>
#include <compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif

//at some point you will want a compositor or server struct

static inline struct weston_surface *
weston_surface_from_resource(struct wl_resource *wl_surface)
{
	return (struct weston_surface *)wl_resource_get_user_data(wl_surface);
}


static inline void
weston_view_map(struct weston_view *view)
{
	view->surface->is_mapped = true;
	view->is_mapped = true;
}

struct wl_client *tw_launch_client(struct weston_compositor *ec, const char *path);
void tw_end_client(struct wl_client *client);




//this API is too long, you can't seriously have all this arguments
//you require three additional arguments to work
bool tw_set_wl_surface(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *surface,
		       struct wl_resource *output,
		       struct wl_listener *surface_destroy_listener);

//two option to manipulate the view
void setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y);
void setup_ui_view(struct weston_view *view, struct weston_layer *layer, int x, int y);





#ifdef  __cplusplus
}
#endif



#endif /* EOF */
