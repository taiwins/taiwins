#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <unistd.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <sequential.h>
#include <wayland-server.h>


#include "taiwins.h"
#include "shell.h"


/**
 * this struct handles the request and sends the event from
 * taiwins_launcher.
 */
struct twcommander {
	// client  info
	char path[256];
	struct wl_client *client;
	struct wl_resource *resource;
	pid_t pid; uid_t uid; gid_t gid;

	struct weston_compositor *compositor;
	struct twshell *shell;
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_listener close_launcher_listener;
};

static struct twcommander onelauncher;

void launcher_surface_destroy_cb(struct wl_listener *listener, void *data)
{
	struct twcommander *launcher = container_of(listener, struct twcommander, close_launcher_listener);
	launcher->surface = NULL;
}


static void
close_launcher(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *wl_buffer,
		       uint32_t exec_id)
{
	fprintf(stderr, "the launcher client is %p\n", client);
	struct twcommander *lch = (struct twcommander *)wl_resource_get_user_data(resource);
	lch->decision_buffer = wl_shm_buffer_get(wl_buffer);
	taiwins_launcher_send_exec(resource, exec_id);
}


static void
set_launcher(struct wl_client *client,
	     struct wl_resource *resource,
	     uint32_t ui_elem,
	     struct wl_resource *wl_surface)
{
	struct twcommander *lch = wl_resource_get_user_data(resource);
	twshell_create_ui_elem(lch->shell, client, ui_elem, wl_surface, NULL, 100, 100, TW_UI_TYPE_WIDGET);
	lch->surface = tw_surface_from_resource(wl_surface);
	wl_resource_add_destroy_listener(wl_surface, &lch->close_launcher_listener);
}


static struct taiwins_launcher_interface launcher_impl = {
	.launch = set_launcher,
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
	struct twcommander *launcher = data;
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
	struct twcommander *lch = data;
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
	struct twcommander *launcher = data;
	launcher->client = tw_launch_client(launcher->compositor, launcher->path);
	wl_client_get_credentials(launcher->client, &launcher->pid, &launcher->uid, &launcher->gid);
}

struct twcommander *announce_commander(struct weston_compositor *compositor,
				       struct twshell *shell, const char *path)
{
	onelauncher.surface = NULL;
	onelauncher.resource = NULL;
	onelauncher.compositor = compositor;
	onelauncher.shell = shell;
	wl_list_init(&onelauncher.close_launcher_listener.link);
	onelauncher.close_launcher_listener.notify = launcher_surface_destroy_cb;

	wl_global_create(compositor->wl_display, &taiwins_launcher_interface, TWDESKP_VERSION, &onelauncher, bind_launcher);
	weston_compositor_add_key_binding(compositor, KEY_P, MODIFIER_CTRL, should_start_launcher, &onelauncher);

	if (path) {
		assert(strlen(path) +1 <= sizeof(onelauncher.path));
		strcpy(onelauncher.path, path);
		struct wl_event_loop *loop = wl_display_get_event_loop(compositor->wl_display);
		wl_event_loop_add_idle(loop, launch_launcher_client, &onelauncher);
	}


	return &onelauncher;
}
