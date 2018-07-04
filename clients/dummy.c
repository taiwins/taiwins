#include <wayland-client.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	const char *wayland_socket = getenv("WAYLAND_SOCKET");
	const int fd = atoi(wayland_socket);

	FILE *log = fopen("/tmp/client-log", "w");
	if (!log)
		return -1;
	struct wl_display *wl_display = wl_display_connect(NULL);
	if (!wl_display)
		fprintf(log, "okay, I don't even have wayland display\n");

	//now what do I do??

}
