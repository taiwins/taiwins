#include <wayland-client.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static void
global_registry_handler(void *data,
			struct wl_registry *registry, uint32_t id,
			const char *interface, uint32_t version)
{
	struct wl_compositor *compositor;
	struct wl_shm *wl_shm;
	//struct you_data structure = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, id,
					      &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wl_shm = wl_registry_bind(registry, id,
					  &wl_shm_interface, version);
	}
}

static void
global_registry_removal(void *data, struct wl_registry *registry, uint32_t id)
{
	fprintf(stdout, "registry removal of %d", id);
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_removal,
};


int main(int argc, char *argv[])
{
	const char *wayland_socket = getenv("WAYLAND_SOCKET");
	const int fd = atoi(wayland_socket);
	FILE *log = fopen("/tmp/client-log", "w");
	if (!log)
		return -1;
	fprintf(log, "%d\n", fd);
	fflush(log);
	fclose(log);

	struct wl_display *wl_display = wl_display_connect(NULL);
	if (!wl_display)
		fprintf(log, "okay, I don't even have wayland display\n");
	struct wl_registry *registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(wl_display);
	wl_display_roundtrip(wl_display);
	while (wl_display_dispatch(wl_display) != -1)
		;
	wl_display_disconnect(wl_display);
	return 0;
}
