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
#include "client.h"

struct output_widgets;
struct desktop_shell;

struct output_widgets {
	struct desktop_shell *shell;
	struct wl_output *output;
	struct app_surface background;
	struct shm_pool pool;

	list_t link;
	bool inited;
};



struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	//right now we only have one output
	list_t outputs;
};


static void
output_init(struct output_widgets *w)
{
	struct taiwins_shell *shell = w->shell->shell;
	w->background = (struct app_surface){0};
	w->background.wl_surface = wl_compositor_create_surface(w->shell->globals.compositor);
	//this maynot be a good idea
	wl_surface_set_user_data(w->background.wl_surface, w);

	taiwins_shell_set_background(shell, w->output, w->background.wl_surface);
	//TODO get the damn image and
	w->inited = true;
}

static void
output_create(struct output_widgets *w, struct wl_output *wl_output, struct desktop_shell *twshell)
{
	w->shell = twshell;
	shm_pool_create(&w->pool, twshell->globals.shm, 4096);
	w->inited = false;
	w->output = wl_output;
	wl_output_set_user_data(wl_output, w);
	if (w->shell->shell)
		output_init(w);
}


static void
output_distroy(struct output_widgets *o)
{
	wl_output_release(o->output);
	wl_output_destroy(o->output);
}


static void
desktop_shell_init(struct desktop_shell *shell)
{
	wl_globals_init(&shell->globals);
	list_init(&shell->outputs);
	shell->shell = NULL;
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);
	struct output_widgets *w, *next;
	list_for_each_safe(w, next, &shell->outputs, link) {
		list_remove(&w->link);
		output_distroy(w);
	}
}



static void shell_configure_surface(void *data,
				    struct taiwins_shell *taiwins_shell,
				    struct wl_surface *surface,
				    uint32_t scale,
				    uint32_t edges,
				    int32_t width,
				    int32_t height)
{
	int32_t w = scale *(width - edges);
	int32_t h = scale *(height - edges);

	struct output_widgets *output = (struct output_widgets *)wl_surface_get_user_data(surface);
	if (surface == output->background.wl_surface) {
		void *buffer_addr = NULL;
		//TODO there is the dilemma: do we want the double buffer here ?
		struct wl_buffer *new_buffer = shm_pool_alloc_buffer(&output->pool, w, h);
		buffer_addr = shm_pool_buffer_access(new_buffer);
		//TODO, copy the content to it
		wl_surface_attach(output->background.wl_surface, new_buffer, 0, 0);
		wl_surface_damage(output->background.wl_surface, 0, 0, w, h);
		wl_surface_commit(output->background.wl_surface);
		if (output->background.wl_buffer) {
			shm_pool_buffer_release(output->background.wl_buffer);
			output->background.wl_buffer = new_buffer;
		}
	} else {

	}
}
/* if (output->background.wl_buffer[0]) */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/* swap(output->background.wl_buffer[0], */
/*      output->background.wl_buffer[1]); */
/* if (!output->background.wl_buffer[0]) */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* else if (shm_pool_buffer_size(output->background.wl_buffer[0]) != w * h * 4) { */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* } */
/* buffer_addr = shm_pool_buffer_access(output->background.wl_buffer[0]); */



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
	struct desktop_shell *twshell = (struct desktop_shell *)data;

	if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		twshell->shell = wl_registry_bind(wl_registry, name, &taiwins_shell_interface, version);
		taiwins_shell_add_listener(twshell->shell, &taiwins_listener, twshell);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct output_widgets *output = malloc(sizeof(*output));
		struct wl_output *wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, version);
		output_create(output, wl_output, twshell);
		list_append(&twshell->outputs, &output->link);

	} else
		wl_globals_announce(&twshell->globals, wl_registry, name, interface, version);
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
	desktop_shell_init(&oneshell);
	//TODO change to wl_display_connect_to_fd
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "couldn't connect to wayland display\n");
		return -1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &oneshell);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	{
		struct output_widgets *w, *next;
		list_for_each_safe(w, next, &oneshell.outputs, link) {
			if (!w->inited)
				output_init(w);
		}
	}

	while(wl_display_dispatch(display) != -1);

	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
