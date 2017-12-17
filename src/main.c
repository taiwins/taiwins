#include <linux/input.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <compositor.h>
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <windowed-output-api.h>
#include <libweston-desktop.h>


static int
tw_log(const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}


int main(int argc, char *argv[])
{
	weston_log_set_handler(tw_log, tw_log);
	struct wl_display *display = wl_display_create();
	struct weston_compositor *compositor = weston_compositor_create(display, NULL);


	weston_compositor_destroy(compositor);
	return 0;
}
