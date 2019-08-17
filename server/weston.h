#ifndef TW_WESTON_PRIVATE_H
#define TW_WESTON_PRIVATE_H

#include <version.h>
#include <compositor.h>

/* this file contains many functions which will become private in the incoming
 * weston release. We have them here to provide to proviate functionanities for
 * taiwins */

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * /brief weston binding table,
 */
struct weston_binding {
	uint32_t key;
	uint32_t button;
	uint32_t axis;
	uint32_t modifier;
	void *handler;
	void *data;
	struct wl_list link;
};


static inline void
weston_destroy_bindings_list(struct wl_list *list)
{
	struct weston_binding *binding, *tmp;
	wl_list_for_each_safe(binding, tmp, list, link)
		weston_binding_destroy(binding);
}

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
