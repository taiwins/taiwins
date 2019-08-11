#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include <libweston-desktop.h>
#include <compositor.h>
#include <wayland-server.h>
#include <sequential.h>
#include <wayland-taiwins-desktop-server-protocol.h>
#include "bindings.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1

struct shell;
struct desktop;
struct console;
struct taiwins_config;

/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 *
 * this adds the signal to destroy the shell as well, add bindings should be here
 */
struct shell *announce_shell(struct weston_compositor *compositor, const char *path,
			     struct taiwins_config *config);
/**
 * /breif annouce console server
 */
struct console *announce_console(struct weston_compositor *compositor,
				 struct shell *shell, const char *exec_path,
				 struct taiwins_config *config);

struct desktop *announce_desktop(struct weston_compositor *compositor,
				 struct taiwins_config *config);
void end_desktop(struct desktop *desktop);


struct wl_client *shell_get_client(struct shell *shell);

void
shell_create_ui_elem(struct shell *shell, struct wl_client *client,
		     uint32_t tw_ui, struct wl_resource *wl_surface,
		     struct wl_resource *tw_output,
		     uint32_t x, uint32_t y, enum tw_ui_type type);






#ifdef  __cplusplus
}
#endif

#endif /* EOF */
