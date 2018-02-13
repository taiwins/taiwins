#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>

#include <sequential.h>

struct output_widget;
struct desktop_shell;





struct desktop_shell {
	struct wl_shm *shm;
	struct wl_compositor *compositor;
	struct taiwins_shell *shell;
	struct {
		struct wl_seat *wl_seat;
		struct wl_keyboard *keyboard;
		struct wl_pointer *pointer;
		struct wl_touch *touch;
		const char *name;
		//keyboard informations
		struct xkb_context *kctxt;
		struct xkb_keymap *kmap;
		struct xkb_state  *kstate;
	} seat;
	//right now we only have one output
	list_t outputs;
};


struct output_widget {
	struct desktop_shell *shell;
	struct wl_output *output;
	struct wl_surface *bg_surface;
	struct wl_surface *pn_surface;
	struct wl_buffer *bg_buffer;
	struct wl_buffer *pn_buffer;
	list_t link;
	bool inited;
};


static void
output_init(struct output_widget *w)
{
	struct desktop_shell *shell = w->shell;
	w->bg_surface = wl_compositor_create_surface(shell->compositor);
	wl_surface_set_user_data(w->bg_surface, w);
	taiwins_shell_set_background(shell->shell, w->output, w->bg_surface);
}

static void
output_create(struct output_widget *w)
{
	if (w->shell) {
		output_init(w);
		w->inited = true;
	}
}


static void
output_distroy(struct wl_output *output)
{
	struct output_widget *o = wl_output_get_user_data(output);
	wl_surface_destroy(o->bg_surface);
	wl_surface_destroy(o->pn_surface);
	wl_buffer_destroy(o->bg_buffer);
	wl_buffer_destroy(o->pn_buffer);
	wl_output_release(o->output);
	wl_output_destroy(o->output);
	list_remove(&o->link);
	free(o);
}



static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    fprintf(stderr, "Format %d\n", format);
}

static struct wl_shm_listener shm_listener = {
	shm_format
};


static void shell_configure_surface(void *data,
				    struct taiwins_shell *taiwins_shell,
				    struct wl_surface *surface,
				    uint32_t scale,
				    uint32_t edges,
				    int32_t width,
				    int32_t height)
{
	struct desktop_shell *twshell = data;
	struct output_widget *output = wl_surface_get_user_data(surface);
	if (surface == output->bg_surface) {
		size_t buff_size = scale * (height - 2 * edges) * scale * (width - 2 * edges) * 4;
		struct wl_shm_pool *pool = wl_shm_create_pool(twshell->shm, int32_t fd,  buff_size);
		//TODO finish the anonymous buffer interface
/		wl_shm_pool_create_buffer(pool, 0, , int32_t height, int32_t stride, uint32_t format)
	} else {

	}
}


static struct taiwins_shell_listener taiwins_listener = {
	.configure = shell_configure_surface
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
		twshell->seat.wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(twshell->seat.wl_seat, NULL, data);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		fprintf(stderr, "announcing the compositor\n");
		twshell->compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)  {
		fprintf(stderr, "got a shm handler\n");
		twshell->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
		wl_shm_add_listener(twshell->shm, &shm_listener, NULL);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct output_widget *output = malloc(sizeof(*output));
		output->output = wl_registry_bind(wl_registry, name, &wl_output_interface, version);
		output->shell = twshell;
		output->inited = false;
		output_create(output);
		list_append(&twshell->outputs, &output->link);


	} else if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		twshell->shell = wl_registry_bind(wl_registry, name, &taiwins_shell_interface, version);
		taiwins_shell_set_user_data(twshell->shell, twshell);
	}
}

static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{

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
