#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <compositor.h>
#include <wayland-server.h>
#include "taiwins.h"
#include <os/os-compatibility.h>

/* static void sigup_handler(int signum) */
/* { */
/*	raise(SIGTERM); */
/* } */

static int
exec_wayland_client(const char *path, int fd)
{
	if (seteuid(getuid()) == -1) {
		return -1;
	}
	//unblock all the signals

	sigset_t allsignals;
	sigfillset(&allsignals);
	sigprocmask(SIG_UNBLOCK, &allsignals, NULL);
	/* struct sigaction sa = { */
	/*	.sa_mask = allsignals, */
	/*	.sa_flags = SA_RESTART, */
	/*	.sa_handler = sigup_handler, */
	/* }; */
	/* there seems to be no actual automatic mechism to notify child process
	 * of the termination, I will have to do it myself. */

	char sn[10];
	int clientid = dup(fd);
	snprintf(sn, sizeof(sn), "%d", clientid);
	setenv("WAYLAND_SOCKET", sn, 1);
	/* const char *for_shell = "/usr/lib/x86_64-linux-gnu/mesa:/usr/lib/x86_64-linux-gnu/mesa-egl:"; */
	/* const char new_env[strlen(for_shell) + 1]; */
	/* strcpy(new_env, for_shell); */
	/* setenv(("LD_LIBRARY_PATH"), new_env, 1); */

	if (execlp(path, path, NULL) == -1) {
		weston_log("failed to execute the client %s\n", path);
		close(clientid);
		return -1;
	}
	return 0;
}

//return the client pid
struct wl_client *
tw_launch_client(struct weston_compositor *ec, const char *path)
{
	int sv[2];
	pid_t pid;
	struct wl_client *client;
	//now we have to do the close on exec thing
	if (os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv)) {
		weston_log("taiwins_client launch: "
			   "failed to create the socket for client `%s`\n",
			   path);
		return NULL;
	}

	pid = fork();
	if (pid == -1) {
		close(sv[0]);
		close(sv[1]);
		weston_log("taiwins_client launch: "
			   "failed to create the process, `%s`\n",
			path);
		return NULL;
	}
	if (pid == 0) {
		//you should close the server end
		close(sv[0]);
		//find a way to exec the child with sv[0];
		//okay, this is basically setting the environment variables
		exec_wayland_client(path, sv[1]);
		//replace this
		exit(-1);
	}
	//for parents, we don't need socket[1]
	close(sv[1]);
	//wayland client launch setup: by leveraging the UNIX
	//socketpair functions. when the socket pair is created. The
	client = wl_client_create(ec->wl_display, sv[0]);
	if (!client) {
		//this can't be happening, but housework need to be done
		close(sv[0]);
		weston_log("taiwins_client launch: "
			   "failed to create wl_client for %s", path);
		return NULL;
	}
	//now we some probably need to add signal on the server side
	return client;
}


void
tw_end_client(struct wl_client *client)
{
	pid_t pid; uid_t uid; gid_t gid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	kill(pid, SIGINT);
}

/*
static int on_term_signal(int signal_number, void *data)
{
	struct wl_display *display = (struct wl_display *)data;
	wl_display_terminate(display);
	return 0;
}
*/

/**
 * @brief general handler for adding a surface.
 *
 * the commited signal for compositor is setting up the view for the surface,
 * which evolves:
 *
 * - (maybe destroying the additional views. You can have two copies of views I
 * guess),
 * - calling weston_view_set_position
 * - calling schedule_repaint for the given output
 *
 * how to properly setup the surface committed callback and destroy signals, I
 * guess I will have to add the some user_data
 */
bool
tw_set_wl_surface(struct wl_client *client,
		  struct wl_resource *resource,
		  struct wl_resource *surface,
		  struct wl_resource *output,
		  struct wl_listener *surface_destroy_listener)
{
	struct weston_surface *wt_surface = weston_surface_from_resource(surface);
	struct weston_view *view, *next;
	if (wt_surface->committed) {
		wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface already have a role");
		return false;
	}
	//we need the commit code.
	wt_surface->committed = NULL;
	wt_surface->committed_private = NULL;
	wl_list_for_each_safe(view, next, &wt_surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(wt_surface);
	wt_surface->output = weston_output_from_resource(output);
	view->output = wt_surface->output;

	//we should have a surface destroy signal
	if (surface_destroy_listener) {
		surface_destroy_listener->notify = NULL;
		wl_signal_add(&wt_surface->destroy_signal,
			      surface_destroy_listener);
	}
	return true;
}

/**
 * configure a static surface for its location, the location is determined,
 * output.x + x, output.y + y
 */
void
setup_static_view(struct weston_view *view, struct weston_layer *layer, int x, int y)
{
	//delete all the other view in the layer, rightnow we assume we only
	//have one view on the layer, this may not be true for UI layer.
	struct weston_view *v, *next;
	wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link) {
		if (v->output == view->output && v != view) {
			weston_view_unmap(v);
			v->surface->committed = NULL;
			weston_surface_set_label_func(v->surface, NULL);
		}
	}
	//the point of calling this function
	weston_view_set_position(view, view->surface->output->x + x, view->surface->output->y + y);
	view->surface->is_mapped = true;
	view->is_mapped = true;

	//for the new created view
	if (wl_list_empty(&view->layer_link.link)) {
		weston_layer_entry_insert(&layer->view_list, &view->layer_link);
		weston_compositor_schedule_repaint(view->surface->compositor);
	}
}


/**
 * same as setup_static_view, excepting deleting all the views in the current surface
 */
void
setup_ui_view(struct weston_view *uiview, struct weston_layer *uilayer, int x, int y)
{
	//on the ui layer, we only have one view per wl_surface
	struct weston_surface *surface = uiview->surface;
	struct weston_output *output = uiview->output;

	struct weston_view *v, *next;

	wl_list_for_each_safe(v, next, &surface->views, surface_link) {
		if (v->output == uiview->output && v != uiview) {
			weston_view_unmap(v);
			v->surface->committed = NULL;
			weston_surface_set_label_func(v->surface, NULL);
		}
	}
	weston_view_set_position(uiview, output->x + x, output->y + y);
	uiview->surface->is_mapped = true;
	uiview->is_mapped = true;
	if (wl_list_empty(&uiview->layer_link.link)) {
		weston_layer_entry_insert(&uilayer->view_list, &uiview->layer_link);
		weston_compositor_schedule_repaint(surface->compositor);
	}
}
