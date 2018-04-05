#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>

#include "weston.h"

//the jobs here are mainly, get to what
struct taiwins_deskop {
	struct weston_layer *actived_workspace;
	//when you
	vector_t workspaces;
};


static void
tw_surface_added(struct weston_desktop_surface *surface,
	      void *user_data)
{
	struct weston_surface *wt_surface = weston_desktop_surface_get_surface(surface);
	//now you need to create the view

	//create
}

static void
tw_surface_removed(struct weston_desktop_surface *surface,
		void *user_data)
{
	//unlink_view
}


static struct weston_desktop_api taiwins_desktop =  {
	.surface_added = tw_surface_added,
	.surface_removed = tw_surface_removed,
};

static void
_free_workspace(void *data)
{
	struct weston_layer *workspace = (struct weston_layer *)data;
	while(workspace->view_list.link.next != &workspace->view_list.link) {
		struct wl_list *link = workspace->view_list.link.next;
		struct weston_view *view = container_of(link, struct weston_view, layer_link.link);
		struct weston_surface *surf = view->surface;
		weston_surface_destroy(surf);
		//close this application!!!! By close the surface?
		//we could get into a segment fault for this
	}
}

bool
taiwins_desktop_init(struct taiwins_deskop *desktop, struct weston_compositor *compositor)
{

	vector_init(&desktop->workspaces, sizeof(struct weston_layer), _free_workspace);
}
