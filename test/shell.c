#include <stdio.h>
#include <string.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>

struct desktop_shell {
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_compositor *compositor;
};


static
void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	struct desktop_shell *twshell = data;
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat0.s = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(seat0.s, &seat_listener, &seat0);
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		fprintf(stderr, "announcing the shell\n");
		gshell = wl_registry_bind(wl_registry, name, &wl_shell_interface, version);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		fprintf(stderr, "announcing the compositor\n");
		twshell->compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)  {
		fprintf(stderr, "got a shm handler\n");
		twshell->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
		wl_shm_add_listener(shm, &shm_listener, NULL);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {

	} else if (strcmp(interface, taiwins_shell_interface.name) == 0) {

	}
}


static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};

int main(int argc, char **argv)
{
	struct desktop_shell oneshell;
	//TODO change to wl_display_connect_to_fd
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		return -1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &oneshell);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
