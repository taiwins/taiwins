/***************************************************************************
 *
 *			How to create the layout
 *
 ***************************************************************************/
#ifndef LAYOUT_H
#define LAYOUT_H

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
	//DPSR_top, //not useful in neither
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
	bool end;
	bool visible;
};


struct layout;
typedef	void (*disposer_fun_t)(const enum disposer_command command,
			       const struct disposer_op *arg,
			       struct weston_view *v, struct layout *l,
			       struct disposer_op *ops);


struct layout {
	bool clean;
	//simplement les N permiere sont visible, tout sera visible si il est -1.
	int nvisible;
	struct wl_list link;
	//if NULL, the layout works on all the output
	struct weston_output *output;
	struct weston_layer *layer;
	disposer_fun_t commander;
};

//the weston_output is not ready when we create it
struct layout *floatlayout_create(struct weston_layer *ly, struct weston_output *o);
void floatlayout_destroy(struct layout *l);




#ifdef  __cplusplus
}
#endif

#endif /* EOF */
