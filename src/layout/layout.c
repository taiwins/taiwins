#include <stdlib.h>

#include "layout.h"




struct floatlayout {
	struct layout;
	size_t width;
	size_t height;
};


struct weston_position
layout_floatpos(struct weston_view *view, struct layout *l)
{
	struct floatlayout *fl = l;
	//this is shall be relative simple
	return struct weston_position {
		rand() % (fl->width / 2), rand() % (fl->height / 2)} ;
}

//this design pose the problem, since you have to run this for all the views in
//the layer.
struct weston_position
layout_master(struct weston_view *view, struct layout *l)
{
	//I don't know what is the order of the current view, I can rely on the
	//position of current view in the layer?
}
