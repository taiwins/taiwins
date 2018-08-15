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
};

//many problems occurs, for example. If we want to implement stacking layout
//that only allows two views to win?

//solution 1) utiliser une coche extra. dans cet facon, on caint pas les view
//recois les input.
//solution 2) utiliser les mask.
//solution 3) mettre les views dans la fin de la coche.

//en fait, je pense que seulement le dernier facon est assez deja, on peut utilise le

//DIFICULTIY: le disposition suive the comportement different. par exemple, les
//disposition flottant n'a pas besoin de reorder the entire layer, while the
//tiling layer usually do.

//we need to build the views


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
