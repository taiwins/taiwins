/***************************************************************************
 *			How to create the
 *
 *
 ***************************************************************************/
#ifndef TAIWINS_H
#define TAIWINS_H

#include <helpers.h>
#include <wayland-server.h>
#include <compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

struct layout {
	struct wl_list link;
	struct weston_output *output;
	uint32_t layer_pos;
	//return the position
	struct weston_position (*layout)(struct weston_view *view, struct layer *l);
};


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
