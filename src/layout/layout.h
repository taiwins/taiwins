/***************************************************************************
 *
 *			How to create the layout
 *
 ***************************************************************************/
#ifndef TAIWINS_H
#define TAIWINS_H

#include <stdbool.h>
#include <helpers.h>
#include <wayland-server.h>
#include <compositor.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/**
 * we decide to use a tree based tiling layout system because its
 * expresiveness. Thus we supports the tree like operations, move the node left,
 * right, up, down.

 * Some of the operation is not ambigous like left/right, delete, top, because
 * we know exactly where the view will be after the operation. But up, down, add
 * have non-determinstic behavoirs.
 */

/* the commands pass to layout algorithm */
enum disposer_command {
	DPSR_focus, //useful in floating.
	DPSR_add,
	DPSR_del,
	DPSR_deplace, //useful in floating.
	DPSR_up, //useful in tiling.
	DPSR_down, //useful in tiling.
	DPSR_left, //useful in tiling.
	DPSR_right, //useful in tiling.
	DPSR_resize, //useful in tiling
	DPSR_top, //not useful in neither
};

/* the operation correspond to the command, I am not sure if it is good to have
 * responce immediately but currently I have no other way and when to apply the
 * operations is not clear either.
 */
struct disposer_op {
	struct weston_view *v;
	struct weston_position pos;
	struct weston_size size;
	float scale;
};


struct layout {
	bool clean;
	//simplement les N permiere sont visible, tout sera visible si il est -1.
	int nvisible;
	struct wl_list link;
	//if NULL, the layout works on all the output
	struct weston_output *output;
	struct weston_layer *layer;
	//retourner le position, mais en fin, on devrait mettre les position
	//pour tout les view dans la coche.
	struct weston_position (*disposer)(struct weston_view *v, struct layout *l);
	//in this way we don't need to allocate the memory on the heap
	void (*commander)(enum disposer_command command, struct weston_view *v, struct layout *l,
			  struct disposer_op *ops);
};


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
