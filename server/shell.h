#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <libweston-desktop.h>
#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-desktop-server-protocol.h>
#include <sequential.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1


//we have this hiden struct for APIs
struct shell;
/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 *
 * this adds the signal to destroy the shell as well.
 */
struct shell *announce_shell(struct weston_compositor *compositor, const char *path);
struct wl_client *shell_get_client(struct shell *shell);

void add_shell_bindings(struct weston_compositor *ec);

void
shell_create_ui_elem(struct shell *shell, struct wl_client *client,
		     uint32_t tw_ui, struct wl_resource *wl_surface,
		     struct wl_resource *tw_output,
		     uint32_t x, uint32_t y, enum tw_ui_type type);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
