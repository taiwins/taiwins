/***************************************************************************
 *			How to create the
 *
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
	struct weston_output *output;
	struct weston_layer *layer;
	uint32_t layer_pos;
	//retourner le position, mais en fin, on devrait mettre les position
	//pour tout les view dans la coche.
	void _reorder(struct layout *l);
};

void
layout_reorder(struct layout *l)
{
	if (l->clean)
		return;
	l->_reorder(l);
	l->clean = true;
}

//many problems occurs, for example. If we want to implement stacking layout
//that only allows two views to win?

//solution 1) utiliser une coche extra. dans cet facon, on caint pas les view
//recois les input.
//solution 2) utiliser les mask.
//solution 3) mettre les views dans la fin de la coche.

//en fait, je pense que seulement le dernier facon est assez deja, on peut utilise le

//DIFICULTIY: le layout suive les behavior different.




#ifdef  __cplusplus
}
#endif

#endif /* EOF */
