#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>

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


bool
taiwins_desktop_init(struct weston_compositor *compositor)
{
//	vector_init(vector_t *v, size_t esize, freefun f)
}
