#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <helpers.h>
#include "weston.h"
#include "config.h"

struct shell;
static struct theme {
	struct weston_compositor *ec;
	struct wl_listener compositor_destroy_listener;
	struct wl_global *global;
	//it can apply to many clients
	struct wl_list clients;
} THEME;


static void
bind_theme(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	wl_client_get_link(client);
	wl_resource_create(client, &tw_theme_interface,
			   tw_theme_interface.version, id);
	//add link to our clienttheme.c
}

static void
end_theme(struct wl_listener *listener, void *data)
{
	struct theme *theme = container_of(listener, struct theme,
					   compositor_destroy_listener);
	wl_global_destroy(theme->global);
}

void
annouce_theme(struct weston_compositor *ec, struct shell *shell,
	      struct taiwins_config *config)
{
	THEME.ec = ec;
	THEME.global = wl_global_create(ec->wl_display, &tw_theme_interface,
					tw_theme_interface.version, &THEME,
					bind_theme);
	wl_list_init(&THEME.clients);

	wl_list_init(&THEME.compositor_destroy_listener.link);
	THEME.compositor_destroy_listener.notify = end_theme;
	wl_signal_add(&ec->destroy_signal, &THEME.compositor_destroy_listener);
}
