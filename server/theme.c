#include <wayland-server.h>

#include <wayland-taiwins-theme-server-protocol.h>
#include <helpers.h>
#include "taiwins.h"
#include "config.h"

struct shell;

struct theme_client {
	struct wl_resource *resource;
	struct wl_list link;
};

union tw_rgba_t {
	uint32_t code;
	struct {
		uint8_t r,g,b,a;
	};
};

struct color_state {
	union tw_rgba_t normal;
	union tw_rgba_t hover;
	union tw_rgba_t active;
};


static struct theme {
	struct weston_compositor *ec;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener config_component;
	struct wl_global *global;
	//it can apply to many clients
	struct wl_list clients;
	//theme data
	struct {
		uint32_t pt_size, *pending_pt_size;
		union tw_rgba_t main_color, *pending_main_color;
		union tw_rgba_t border_color, *pending_border_color;
		//text
		union tw_rgba_t text_color, *pending_text_color;
		union tw_rgba_t text_active_color, *pending_text_active_color;
		//widget
		struct color_state button, *pending_button;
	};
} THEME;



static void
unbind_theme(struct wl_resource *resource)
{
	struct theme_client *tc = wl_resource_get_user_data(resource);
	wl_list_remove(&tc->link);
	free(tc);
}

static void
bind_theme(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct theme *theme = data;
	struct theme_client *tc;
	struct wl_resource *resource =
		wl_resource_create(client, &tw_theme_interface,
				   tw_theme_interface.version, id);
	tc = zalloc(sizeof(struct theme_client));
	if (!tc) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_NO_MEMORY,
				       "failed to create theme for client %d", id);
		wl_resource_destroy(resource);
	}
	//theme does not have requests
	wl_resource_set_implementation(resource, NULL, tc, unbind_theme);

	tc->resource = resource;
	wl_list_init(&tc->link);
	wl_list_insert(&theme->clients, &tc->link);

	//right now we send all the
	tw_theme_send_start(resource);
	tw_theme_send_done(resource);
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
