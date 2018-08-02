#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>


#include "taiwins.h"
#include "desktop.h"


/**
 * this struct handles the request and sends the event from
 * taiwins_launcher.
 */
struct twlauncher {
	// client  info
	char path[256];
	struct wl_client *client;
	struct wl_resource *resource;
	pid_t pid; uid_t uid; gid_t gid;

	struct weston_compositor *compositor;
	struct twshell *shell;
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_listener close_listener;
	struct wl_resource *callback;
	unsigned int exec_id;
};

static struct twlauncher onelauncher;



static void
close_launcher_notify(struct wl_listener *listener, void *data)
{
	struct twlauncher *lch = container_of(listener, struct twlauncher, close_listener);
	twshell_close_ui_surface(lch->surface);
	wl_list_remove(&lch->close_listener.link);
}



static void
close_launcher(struct wl_client *client, struct wl_resource *resource,
	       struct wl_resource *wl_buffer)
{
	struct twlauncher *lch = (struct twlauncher *)wl_resource_get_user_data(resource);
	tw_lose_surface_focus(lch->surface);
	lch->decision_buffer = wl_shm_buffer_get(wl_buffer);
	struct weston_output *output = lch->surface->output;
	wl_signal_add(&output->frame_signal, &lch->close_listener);

	wl_callback_send_done(lch->callback, lch->exec_id);
	wl_resource_destroy(lch->callback);

}


static void
set_launcher(struct wl_client *client, struct wl_resource *resource,
	     struct wl_resource *wl_surface,
	     uint32_t exec_callback, uint32_t exec_id)
{
	struct twlauncher *lch = wl_resource_get_user_data(resource);
	lch->surface = tw_surface_from_resource(wl_surface);
	lch->callback = wl_resource_create(client, &wl_callback_interface, 1, exec_callback);
	lch->exec_id = exec_id;

	twshell_set_ui_surface(lch->shell, lch->surface,
			       tw_get_default_output(lch->compositor),
			       resource, 100, 100);
}


static struct taiwins_launcher_interface launcher_impl = {
	.set_launcher = set_launcher,
	.submit = close_launcher
};


static void
unbind_launcher(struct wl_resource *r)
{

}


static void
bind_launcher(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	/* int pid, uid, gid; */
	/* wl_client_get_credentials(client, &pid, &uid, &gid); */
	struct twlauncher *launcher = data;
	struct wl_resource *wl_resource = wl_resource_create(client, &taiwins_launcher_interface,
							  TWDESKP_VERSION, id);

	uid_t uid; gid_t gid; pid_t pid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (launcher->client &&
	    (uid != launcher->uid || pid != launcher->pid || gid != launcher->gid)) {
		wl_resource_post_error(wl_resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "client %d is not un atherized launcher", id);
		wl_resource_destroy(wl_resource);
	}

	launcher->resource = wl_resource;
	wl_resource_set_implementation(wl_resource, &launcher_impl, launcher, unbind_launcher);
}

static void
should_start_launcher(struct weston_keyboard *keyboard,
		      const struct timespec *time, uint32_t key,
		      void *data)
{
	struct twlauncher *lch = data;
	if ((lch->surface) && wl_list_length(&lch->surface->views))
		return;
	fprintf(stderr, "should start launcher\n");
	taiwins_launcher_send_start(lch->resource,
				    wl_fixed_from_int(200),
				    wl_fixed_from_int(200),
				    wl_fixed_from_int(1));
}


static void
launch_launcher_client(void *data)
{
	struct twlauncher *launcher = data;
	launcher->client = tw_launch_client(launcher->compositor, launcher->path);
	wl_client_get_credentials(launcher->client, &launcher->pid, &launcher->uid, &launcher->gid);
}



struct twlauncher *announce_twlauncher(struct weston_compositor *compositor,
				       struct twshell *shell, const char *path)
{
	onelauncher.surface = NULL;
	onelauncher.resource = NULL;
	onelauncher.compositor = compositor;
	onelauncher.shell = shell;
	wl_list_init(&onelauncher.close_listener.link);
	onelauncher.close_listener.notify = close_launcher_notify;

	wl_global_create(compositor->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onelauncher, bind_launcher);
	weston_compositor_add_key_binding(compositor, KEY_P, 0, should_start_launcher, &onelauncher);

	if (path) {
		assert(strlen(path) +1 <= sizeof(onelauncher.path));
		strcpy(onelauncher.path, path);
		struct wl_event_loop *loop = wl_display_get_event_loop(compositor->wl_display);
		wl_event_loop_add_idle(loop, launch_launcher_client, &onelauncher);
	}


	return &onelauncher;
}
