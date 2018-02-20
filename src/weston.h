#ifndef TW_WESTON_H
#define TW_WESTON_H

#include <wayland-server.h>
#include <compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif


static inline struct weston_surface *
weston_surface_from_resource(struct wl_resource *wl_surface)
{
	return (struct weston_surface *)wl_resource_get_user_data(wl_surface);
}


#ifdef  __cplusplus
}
#endif



#endif /* EOF */
